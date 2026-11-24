/** \file
 * fps24 for the R6 1.9.0: Canon's own "24.00p" row in Movie rec. quality, hidden in this
 * firmware, shown again; SET toggles Disable / Enable and Canon stores the choice in its
 * backed-up camera setting. The movie frame rate does not change yet (the movie parameter
 * builder ignores the setting); recording true 24.00p is the next step
 * (research/opengate/fps24/FPS24.md 7).
 *
 * Every change is a ROM word, patched through apply_patches() in 4 groups (one patch
 * each), all or nothing, after a stock check of every word. Applied and removed in
 * LiveView only: Canon's menu dialogs read the row when they open, and nothing here may
 * change under an open dialog.
 *
 * Values and their sources: fps24_r6_190.h.
 */

#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <patch.h>
#include <config.h>

#include "fps24_r6_190.h"

/* src/patch_mmu.c (not declared in patch.h for modules) */
extern int mmu_reserve_runtime_pages(uint32_t total_pages, uint32_t total_L2_tables);

#define RD32(a) (*(volatile uint32_t *)(a))

static CONFIG_INT("fps24.row", f24_ui, 0);     /* menu, saved */
static int f24_in;                              /* the patches are in */
static const char * f24_why = "";               /* why the last apply was refused */

struct f24_group { const struct f24_word * w; int n; };
static const struct f24_group f24_groups[] =
{
    { f24_p1, COUNT(f24_p1) },
    { f24_p2, COUNT(f24_p2) },
    { f24_p3, COUNT(f24_p3) },
    { f24_p4n, COUNT(f24_p4n) },
};
#define F24_MAX_WORDS 16

static int f24_ram_ok(uint32_t a, uint32_t n)
{
    if (a & 3) return 0;
    if (a >= 0x1000 && a + n < 0x40000000) return 1;
    if (a >= 0x40001000 && a + n < 0x80000000) return 1;
    return 0;
}

static uint32_t f24_rd(uint32_t a)
{
    return f24_ram_ok(a, 4) ? RD32(a) : 0;
}

/* Canon's stored setting (1 = Enable), -1 if not reachable */
static int f24_stored(void)
{
    uint32_t cc = f24_rd(f24_rd(F24_CC_PTR) + 8), model = f24_rd(cc + F24_MODEL_OFF);
    return f24_ram_ok(model, F24_STORE_OFF + 4) ? (int) f24_rd(model + F24_STORE_OFF) : -1;
}

static int f24_opengate_in(void)
{
    return RD32(F24_OPENGATE_WORD) != F24_OPENGATE_STOCK;
}

/* LiveView, no playback, no recording: no Canon menu dialog is open */
static int f24_can_change(void)
{
    return lv && !is_play_or_qr_mode() && !RECORDING;
}

static int f24_apply(void)
{
    if (f24_opengate_in()) { f24_why = "Turn open gate off (then reboot)"; return 0; }
    struct patch p[COUNT(f24_groups)];
    uint32_t bufs[COUNT(f24_groups)][2][F24_MAX_WORDS];     /* old / new bytes */
    for (int g = 0; g < COUNT(f24_groups); g++)
    {
        const struct f24_group * gr = &f24_groups[g];
        if (gr->n > F24_MAX_WORDS) { f24_why = "internal: group too long"; return 0; }
        for (int i = 0; i < gr->n; i++)
        {
            const struct f24_word * w = &gr->w[i];
            if (w->addr != gr->w[0].addr + 4 * i || RD32(w->addr) != w->stock)
            {
                printf("fps24: %08x is %08x, not %08x\n", w->addr, RD32(w->addr), w->stock);
                f24_why = "ROM not stock (other firmware or patch?)";
                return 0;
            }
            bufs[g][0][i] = w->stock;
            bufs[g][1][i] = w->val;
        }
        p[g] = gr->n == 1
            ? (struct patch) { .addr = (uint8_t *) gr->w[0].addr, .old_value = gr->w[0].stock,
                               .new_value = gr->w[0].val, .size = 4, .is_instruction = gr->w[0].insn,
                               .description = gr->w[0].what }
            : (struct patch) { .addr = (uint8_t *) gr->w[0].addr, .old_values = (uint8_t *) bufs[g][0],
                               .new_values = (uint8_t *) bufs[g][1], .size = 4 * gr->n,
                               .is_instruction = gr->w[0].insn, .description = gr->w[0].what };
    }
    /* adds nothing if the pool is already this big (then its pages may be taken: reboot) */
    if (mmu_reserve_runtime_pages(F24_PAGES, F24_L2)) { f24_why = "Not enough memory for the patches"; return 0; }
    int e = apply_patches(p, COUNT(p));         /* copies the arrays; all or nothing */
    if (e)
    {
        f24_why = e == E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active"
                : e == E_PATCH_BAD_MMU_PAGE ? "No MMU page left (reboot if open gate was on)"
                : "ROM patch refused";
        return 0;
    }
    f24_in = 1;
    f24_why = "";
    return 1;
}

static void f24_remove(void)
{
    for (int g = COUNT(f24_groups) - 1; g >= 0; g--)
    {
        int e = unpatch_memory(f24_groups[g].w[0].addr);
        if (e) printf("fps24: unpatch %08x: %x\n", f24_groups[g].w[0].addr, e);
    }
    f24_in = 0;
}

static void f24_task(void * unused)
{
    TASK_LOOP
    {
        msleep(200);
        if (ml_shutdown_requested) break;
        if (!f24_ui == !f24_in || !f24_can_change()) continue;
        if (!f24_ui) { f24_remove(); continue; }
        if (!f24_apply())
        {
            f24_ui = 0;
            NotifyBox(5000, "24.00p row refused:\n%s", f24_why);
        }
    }
}

/* ------------------------------------------------------------------------
 * menu
 * ------------------------------------------------------------------------ */

static MENU_UPDATE_FUNC(f24_update)
{
    int s = f24_stored();
    if (s >= 0) MENU_SET_RINFO("Canon: %s", s ? "Enable" : "Disable");
    if (!f24_ui != !f24_in)
        MENU_SET_WARNING(MENU_WARN_INFO, "Takes effect in LiveView (not in playback or while recording).");
    else if (!f24_ui && f24_why[0])
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Refused: %s", f24_why);
    else if (f24_in)
        MENU_SET_WARNING(MENU_WARN_INFO, "Row in Canon's Movie rec. quality. The frame rate stays 23.98p for now.");
}

static struct menu_entry f24_menu[] =
{
    {
        .name   = "Canon 24.00p row",
        .priv   = &f24_ui,
        .max    = 1,
        .update = f24_update,
        .help   = "Shows Canon's hidden 24.00p row in Movie rec. quality; Canon stores the choice.",
        .help2  = "Work in progress: movies stay 23.98p for now. Not with open gate.",
    },
};

/* ------------------------------------------------------------------------
 * module
 * ------------------------------------------------------------------------ */

static unsigned int f24_init(void)
{
    if (!is_camera("R6", "*"))
    {
        printf("fps24: R6 only\n");
        return CBR_RET_ERROR;
    }
    menu_add("Movie", f24_menu, COUNT(f24_menu));
    task_create("fps24_task", 0x1d, 0x1000, f24_task, 0);
    return 0;
}

static unsigned int f24_deinit(void)
{
    /* at power off touch nothing: the patches are in RAM, stock again at the next boot */
    if (ml_shutdown_requested) return 0;
    if (f24_in && f24_can_change()) f24_remove();
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(f24_init)
    MODULE_DEINIT(f24_deinit)
MODULE_INFO_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(f24_ui)
MODULE_CONFIGS_END()
