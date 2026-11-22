/** \file
 * Movie heat timer: slow down Canon's time-based movie overheat timer.
 *
 * The R6 has two separate overheat mechanisms:
 * - temperature sensors with warning / stop / shutdown thresholds;
 * - a timer: a budget that drains at a fixed rate per movie mode, taken
 *   from a table in ROM. It stops 4K recording after 30 / 40 min even
 *   when the camera is cool, e.g. with a fan.
 *
 * This option only multiplies the seconds of every row of the timer's
 * table (HEAT_TABLE_* in the platform's consts.h), so the budget drains
 * more slowly. Canon's logic, the stop flags, the sensor thresholds and
 * the temperature status are not touched: the camera still warns, stops
 * and shuts down on real temperatures. "Canon" restores the original
 * table.
 */

#include "dryos.h"
#include "menu.h"
#include "config.h"
#include "property.h"
#include "tasks.h"
#include "patch.h"

#ifdef FEATURE_MOVIE_HEAT_TIMER

#ifndef CONFIG_MMU_REMAP
#error "FEATURE_MOVIE_HEAT_TIMER patches ROM data, it needs CONFIG_MMU_REMAP"
#endif

#define HEAT_TIMER_MAX_FACTOR 4

// One patch covers the seconds of all rows, from the standby seconds of
// the first row to the recording seconds of the last one. The bytes in
// between (keys, flags, the rest of each row) are written back unchanged.
#define HEAT_PATCH_ADDR (HEAT_TABLE_ADDR + HEAT_TABLE_STANDBY)
#define HEAT_PATCH_END  (HEAT_TABLE_ADDR + (HEAT_TABLE_ROWS - 1) * HEAT_TABLE_ROW_SIZE \
                         + HEAT_TABLE_RECORDING + 4)
#define HEAT_PATCH_SIZE (HEAT_PATCH_END - HEAT_PATCH_ADDR)

#if (HEAT_TABLE_ADDR % 4) || (HEAT_TABLE_ROW_SIZE % 4) \
    || (HEAT_TABLE_STANDBY % 4) || (HEAT_TABLE_RECORDING % 4)
#error "The heat table seconds must be 32-bit aligned"
#endif
#if (HEAT_PATCH_ADDR >> 16) != ((HEAT_PATCH_END - 1) >> 16)
#error "patch_mmu can't patch across a 64 KiB page"
#endif

static const uint32_t heat_canon_seconds[HEAT_TABLE_ROWS][2] = HEAT_TABLE_SECONDS;

// 1 = Canon's table, 2 .. HEAT_TIMER_MAX_FACTOR = seconds multiplied by it
static CONFIG_INT("movie.heat_timer", heat_timer_factor, 1);

// factor patched in ROM right now, 1 = nothing patched
static int heat_timer_applied = 1;
static int heat_timer_error = 0;
static struct semaphore *heat_timer_sem = NULL;

// patched copy of the table; apply_patches() keeps its own copy
static uint32_t heat_patch_buf[HEAT_PATCH_SIZE / 4];

// Canon's model uses the recording seconds while PROP_SHOOTING_TYPE is 4
// (0xE04E8440 on R6). RECORDING alone is not enough there: R6 does not
// read PROP_MVR_REC_START (prop_handler_deny).
static int heat_timer_recording(void)
{
    return RECORDING || shooting_type == 4;
}

static uint32_t heat_rom_word(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

// 1 if every row still holds Canon's seconds (from consts.h)
static int heat_table_is_canon(void)
{
    for (int i = 0; i < HEAT_TABLE_ROWS; i++)
    {
        uint32_t row = HEAT_TABLE_ADDR + i * HEAT_TABLE_ROW_SIZE;
        if (heat_rom_word(row + HEAT_TABLE_STANDBY) != heat_canon_seconds[i][0]
            || heat_rom_word(row + HEAT_TABLE_RECORDING) != heat_canon_seconds[i][1])
        {
            return 0;
        }
    }
    return 1;
}

// Never below Canon's value, never above HEAT_TABLE_SECONDS_MAX
// (see consts.h for the arithmetic it protects).
static uint32_t heat_scale(uint32_t seconds, int factor)
{
    if (seconds >= HEAT_TABLE_SECONDS_MAX)
        return seconds;
    if (seconds * factor > HEAT_TABLE_SECONDS_MAX)
        return HEAT_TABLE_SECONDS_MAX;
    return seconds * factor;
}

// Patch the table for this factor (1 = Canon's table).
// Returns 0, or E_PATCH_* / E_UNPATCH_* flags.
static int heat_timer_set(int factor)
{
    int err = 0;
    take_semaphore(heat_timer_sem, 0);

    if (factor == heat_timer_applied)
        goto end;

    if (heat_timer_applied != 1)
    {
        err = unpatch_memory(HEAT_PATCH_ADDR);
        if (err)
            goto end;
        heat_timer_applied = 1;
    }

    if (factor == 1)
        goto end;

    if (!heat_table_is_canon())
    {
        err = E_PATCH_OLD_VALUE_MISMATCH;
        goto end;
    }

    memcpy(heat_patch_buf, (void *)HEAT_PATCH_ADDR, HEAT_PATCH_SIZE);
    for (int i = 0; i < HEAT_TABLE_ROWS; i++)
    {
        uint32_t row = i * HEAT_TABLE_ROW_SIZE;
        uint32_t *standby = &heat_patch_buf[row / 4];
        uint32_t *recording = &heat_patch_buf[(row + HEAT_TABLE_RECORDING - HEAT_TABLE_STANDBY) / 4];
        *standby = heat_scale(*standby, factor);
        *recording = heat_scale(*recording, factor);
    }

    struct patch patch =
    {
        .addr = (uint8_t *)HEAT_PATCH_ADDR,
        // the current content, just checked against Canon's values;
        // apply_patches() saves a copy of it for unpatch_memory()
        .old_values = (uint8_t *)HEAT_PATCH_ADDR,
        .new_values = (uint8_t *)heat_patch_buf,
        .size = HEAT_PATCH_SIZE,
        .description = "Heat timer: seconds of Canon's movie heat table",
        .is_instruction = 0,
    };
    err = apply_patches(&patch, 1);
    if (!err)
        heat_timer_applied = factor;

end:
    heat_timer_error = err;
    give_semaphore(heat_timer_sem);
    return err;
}

// On error the setting follows what is really patched (normally Canon's table).
static void heat_timer_change(int factor)
{
    int err = heat_timer_set(factor);
    if (err)
    {
        heat_timer_factor = heat_timer_applied;
        if (heat_timer_applied == 1)
            NotifyBox(5000, "Heat timer: patch error 0x%x,\nCanon's timer is used.", err);
        else
            NotifyBox(5000, "Heat timer: error 0x%x,\nstill %dx.", err, heat_timer_applied);
    }
}

static MENU_SELECT_FUNC(heat_timer_select)
{
    if (heat_timer_recording())
        return;

    int factor = heat_timer_applied + delta;
    if (factor > HEAT_TIMER_MAX_FACTOR)
        factor = 1;
    if (factor < 1)
        factor = HEAT_TIMER_MAX_FACTOR;

    heat_timer_factor = factor;
    heat_timer_change(factor);
}

static MENU_UPDATE_FUNC(heat_timer_update)
{
    if (heat_timer_applied == 1)
        MENU_SET_VALUE("Canon");
    else
        MENU_SET_VALUE("%dx", heat_timer_applied);

    MENU_SET_ENABLED(heat_timer_applied != 1);

    if (heat_timer_recording())
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Can't change the heat timer while recording.");
    else if (heat_timer_error)
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Patch error 0x%x, see Debug > Memory patches.", heat_timer_error);
}

static struct menu_entry heat_timer_menu[] =
{
    {
        .name       = "Heat timer",
        .priv       = &heat_timer_factor,
        .min        = 1,
        .max        = HEAT_TIMER_MAX_FACTOR,
        .select     = heat_timer_select,
        .update     = heat_timer_update,
        .help       = "Slow down Canon's time-based movie overheat timer.",
        .help2      = "Temp sensors still warn, stop and shut down the camera.",
    },
};

static void heat_timer_task()
{
    extern int ml_started;
    while (!ml_started)
        msleep(100);

    // saved setting
    int factor = heat_timer_factor;
    if (factor < 1 || factor > HEAT_TIMER_MAX_FACTOR)
        factor = 1;
    heat_timer_factor = factor;
    if (factor != 1)
        heat_timer_change(factor);
}

static void heat_timer_init()
{
    heat_timer_sem = create_named_semaphore("heat_timer", SEM_CREATE_UNLOCKED);
    menu_add("Movie", heat_timer_menu, COUNT(heat_timer_menu));
}

INIT_FUNC(__FILE__, heat_timer_init);
TASK_CREATE("heat_timer_task", heat_timer_task, 0, 0x1e, 0x1000);

#endif // FEATURE_MOVIE_HEAT_TIMER
