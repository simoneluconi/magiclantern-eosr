/** \file
 * fps24 for the R6 1.9.0: true 24.00p in 4K UHD through Canon's own "24.00p" setting
 * (research/opengate/fps24/FPS24.md 3.1 and 7).
 *
 * Canon's "24.00p" row in Movie rec. quality, hidden in this firmware, is shown again; SET
 * toggles Disable / Enable and Canon stores the choice in its backed-up camera setting.
 * With Enable, Canon's movie parameter builder (patched) publishes 4K UHD 23.98p as 24.00p:
 * fps index 9, the sensor locked to the 60.00 Hz base sync, a 24000/1000 time base. FHD and
 * every other rate are built as stock. Nothing persists but Canon's own setting, which a
 * stock builder ignores: a boot without ML records 23.98p.
 *
 * Camera: with P1-P5 24.00p goes live (fps index 9, 60.00 Hz sync) but REC stays BUSY:
 * the GUI's record drive request still asked for 23.98p (P6 fixes it, in only while 24.00p
 * is live), and the colour modules found no parameters for fps index 9 (P7: as 23.98p).
 *
 * Apply (FPS24.md 7.3): check every stock word and the timing table pointer, fill the 8
 * timing cells of column 9 (RAM, only if still 0), then the 5 ROM patches in one
 * apply_patches() call (all or nothing; it checks the stock bytes again).
 * Remove: the builder patch first; the row stays until Canon's movie parameters are no
 * longer 24.00 (one rebuild: the user toggles the row or changes the frame rate). The
 * timing cells are never zeroed: they are RAM, rebuilt at boot, and used only at fps
 * index 9.
 * Everything happens in LiveView only (no Canon dialog open, not recording); nothing at
 * power off.
 *
 * Values and their sources: fps24_r6_190.h.
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <menu.h>
#include <patch.h>
#include <config.h>

#include "fps24_r6_190.h"

/* src/patch_mmu.c (not declared in patch.h for modules) */
extern int mmu_reserve_runtime_pages(uint32_t total_pages, uint32_t total_L2_tables);

#define RD16(a) (*(volatile uint16_t *)(a))
#define RD32(a) (*(volatile uint32_t *)(a))

static CONFIG_INT("fps24.row", f24_ui, 0);     /* menu, saved */
static const char * f24_why = "";               /* why the last apply was refused */

/* in apply order; removal goes the same way, the builder patch first */
struct f24_group { const struct f24_word * w; int n; };
static const struct f24_group f24_groups[] =
{
    { f24_p5, COUNT(f24_p5) },
    { f24_p7, COUNT(f24_p7) },
    { f24_p4n, COUNT(f24_p4n) },
    { f24_p3, COUNT(f24_p3) },
    { f24_p2, COUNT(f24_p2) },
    { f24_p1, COUNT(f24_p1) },
};
#define F24_NG COUNT(f24_groups)
#define F24_G_P5 0                              /* the builder patch: the frame rate itself */
#define F24_MAX_WORDS 28
static int f24_g_in[F24_NG];                    /* group applied */

static int f24_p5_in(void)  { return f24_g_in[F24_G_P5]; }
static int f24_row_in(void)
{
    for (int g = 0; g < F24_NG; g++) if (g != F24_G_P5 && f24_g_in[g]) return 1;
    return 0;
}
static int f24_all_in(void)
{
    for (int g = 0; g < F24_NG; g++) if (!f24_g_in[g]) return 0;
    return 1;
}

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

/* Canon's current movie parameters (PROP_MOVIE_CONTROL_PARAM, core) */
static int f24_live(void)       /* 24.00p is what the camera records now */
{
    return video_mode_fps_x100 == 2400 && video_mode_size_code == 5;
}
static int f24_target(void)     /* 4K UHD 23.98p (NTSC), the only setting changed */
{
    return video_mode_size_code == 5 && (video_mode_fps_x100 == 2398 || video_mode_fps_x100 == 2400);
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

/* ------------------------------------------------------------------------
 * timing table, row 15, column 9
 * ------------------------------------------------------------------------ */

static uint32_t f24_cell_addr(const struct f24_cell * c, int col)
{
    return c->size == 4
        ? F24_TIMING_TABLE + 0x1180 + F24_ROW * 0x40 + 4 * col
        : F24_TIMING_TABLE + c->off + F24_ROW * 0x20 + 2 * col;
}

static uint32_t f24_cell_get(const struct f24_cell * c, int col)
{
    uint32_t a = f24_cell_addr(c, col);
    return c->size == 2 ? RD16(a) : RD32(a);
}

/* checks the whole row (column 4 stock, column 9 still 0 or already ours), then fills the
 * zeros of column 9. Returns 0 with f24_why set if anything is unexpected (nothing written) */
static int f24_fill_cells(void)
{
    if (RD32(F24_TIMING_INFO + 0x20) != F24_TIMING_TABLE)
    {
        f24_why = "Canon timing table moved";
        return 0;
    }
    for (int i = 0; i < COUNT(f24_cells); i++)
    {
        const struct f24_cell * c = &f24_cells[i];
        uint32_t v4 = f24_cell_get(c, F24_COL_2398), v9 = f24_cell_get(c, F24_COL_2400);
        if (v4 != c->col4 || (v9 != 0 && v9 != c->val))
        {
            printf("fps24: timing %s col 4 = %d, col 9 = %d\n", c->what, v4, v9);
            f24_why = "Timing table not stock";
            return 0;
        }
    }
    for (int i = 0; i < COUNT(f24_cells); i++)
    {
        const struct f24_cell * c = &f24_cells[i];
        uint32_t a = f24_cell_addr(c, F24_COL_2400);
        if (f24_cell_get(c, F24_COL_2400) != 0) continue;
        if (c->size == 2) RD16(a) = c->val;
        else RD32(a) = c->val;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * ROM patches
 * ------------------------------------------------------------------------ */

static uint32_t f24_bufs[F24_NG][2][F24_MAX_WORDS];     /* old / new bytes (apply_patches copies) */

/* applies every group not in yet, all or nothing */
static int f24_apply(void)
{
    if (f24_opengate_in()) { f24_why = "Turn the open gate feature OFF first"; return 0; }

    struct patch p[F24_NG];
    int np = 0, gs[F24_NG];
    for (int g = 0; g < F24_NG; g++)
    {
        if (f24_g_in[g]) continue;
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
            f24_bufs[g][0][i] = w->stock;
            f24_bufs[g][1][i] = w->val;
        }
        p[np] = gr->n == 1
            ? (struct patch) { .addr = (uint8_t *) gr->w[0].addr, .old_value = gr->w[0].stock,
                               .new_value = gr->w[0].val, .size = 4, .is_instruction = gr->w[0].insn,
                               .description = gr->w[0].what }
            : (struct patch) { .addr = (uint8_t *) gr->w[0].addr, .old_values = (uint8_t *) f24_bufs[g][0],
                               .new_values = (uint8_t *) f24_bufs[g][1], .size = 4 * gr->n,
                               .is_instruction = gr->w[0].insn, .description = gr->w[0].what };
        gs[np++] = g;
    }
    if (!np) return 1;

    /* the timing cells first: unused until fps index 9, so harmless if the patches fail */
    if (!f24_fill_cells()) return 0;

    /* adds nothing if the pool is already this big (then its pages may be taken) */
    if (mmu_reserve_runtime_pages(F24_PAGES, F24_L2)) { f24_why = "Not enough memory for the patches"; return 0; }
    int e = apply_patches(p, np);               /* copies the arrays; all or nothing */
    if (e)
    {
        f24_why = e & E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active"
                : e & E_PATCH_BAD_MMU_PAGE ? "No MMU page left (reboot if open gate was on)"
                : e & E_PATCH_OLD_VALUE_MISMATCH ? "ROM not stock (other firmware or patch?)"
                : "ROM patch refused";
        printf("fps24: apply_patches %x\n", e);
        return 0;
    }
    for (int k = 0; k < np; k++) f24_g_in[gs[k]] = 1;
    f24_why = "";
    return 1;
}

/* P6, the GUI's record drive request, follows the live state: in while Canon's movie
 * parameters are 24.00 (REC then asks for fps index 9, as the LiveView runs), out otherwise
 * (stock request, index 4, as the LiveView runs). So the two agree in every steady state,
 * also while 24.00p is still live after the entry was turned off */
static int f24_p6_in;
static uint32_t f24_p6_buf[2][COUNT(f24_p6)];

static void f24_p6_step(void)
{
    int want = f24_live();
    if (want == f24_p6_in) return;
    if (!want)
    {
        int e = unpatch_memory(f24_p6[0].addr);
        if (e) printf("fps24: unpatch P6: %x\n", e);
        f24_p6_in = 0;
        return;
    }
    for (int i = 0; i < COUNT(f24_p6); i++)
    {
        if (f24_p6[i].addr != f24_p6[0].addr + 4 * i || RD32(f24_p6[i].addr) != f24_p6[i].stock)
        {
            printf("fps24: P6 %08x is %08x, not %08x\n", f24_p6[i].addr, RD32(f24_p6[i].addr), f24_p6[i].stock);
            return;
        }
        f24_p6_buf[0][i] = f24_p6[i].stock;
        f24_p6_buf[1][i] = f24_p6[i].val;
    }
    struct patch p = { .addr = (uint8_t *) f24_p6[0].addr, .old_values = (uint8_t *) f24_p6_buf[0],
                       .new_values = (uint8_t *) f24_p6_buf[1], .size = 4 * COUNT(f24_p6),
                       .is_instruction = 1, .description = f24_p6[0].what };
    int e = apply_patches(&p, 1);
    if (e) { printf("fps24: P6 apply %x\n", e); return; }
    f24_p6_in = 1;
}

static void f24_remove(int from, int to)
{
    for (int g = from; g <= to; g++)
    {
        if (!f24_g_in[g]) continue;
        int e = unpatch_memory(f24_groups[g].w[0].addr);
        if (e) printf("fps24: unpatch %08x: %x\n", f24_groups[g].w[0].addr, e);
        f24_g_in[g] = 0;
    }
}

/* ------------------------------------------------------------------------
 * debug dump (menu "Debug dump", off by default): while 24.00p is live, Canon's own
 * DryosDebugMsg RAM ring (S = *0x52A8, "KUWA" handles at S+0x10/0x14/0x18; PLAYER-2736.md
 * 9) goes to ML/LOGS/F24DM0.BIN / F24DM1.BIN alternately every 2 s, in the OGDM format
 * that player/dmring.py decodes. Camera: REC at 24.00p leaves Canon "BUSY" for good with
 * the camera alive, so the ring shows what the record start waits for. No patch.
 * ------------------------------------------------------------------------ */

static CONFIG_INT("fps24.debug", f24_debug, 0);

#define F24_DM_PTR    0x52A8
#define F24_DM_S_LEN  0x348
#define F24_DM_MAGIC  0x4157554B
#define F24_DM_SLOT   0x54

static uint32_t f24_dm_ring(uint32_t s, int k, uint32_t * len)
{
    uint32_t h = f24_rd(s + 0x10 + 4 * k);
    if (!f24_ram_ok(h, 0x80) || f24_rd(h) != F24_DM_MAGIC) return 0;
    uint32_t n = 0;
    for (int sub = 0; sub < 2; sub++)
    {
        uint32_t off = f24_rd(h + 4 * (2 + 15 * sub)), cnt = f24_rd(h + 4 * (4 + 15 * sub));
        if (off < 0x80 || off > 0x100000 || cnt > 0x4000) return 0;
        n = MAX(n, off + cnt * F24_DM_SLOT);
    }
    if (!f24_ram_ok(h, n)) return 0;
    *len = n;
    return h;
}

static void f24_dm_dump(const char * name)
{
    uint32_t s = f24_rd(F24_DM_PTR);
    if (!f24_ram_ok(s, F24_DM_S_LEN) || !f24_rd(s + 0x0C)) return;
    uint32_t h[3], n[3], nb = 1;
    for (int k = 0; k < 3; k++) { h[k] = f24_dm_ring(s, k, &n[k]); if (h[k]) nb++; }
    FILE * f = FIO_CreateFile(name);
    if (!f) return;
    uint32_t hdr[4] = { 0x4D44474F, 1, s, nb };    /* "OGDM", version, S, blocks */
    FIO_WriteFile(f, hdr, sizeof(hdr));
    uint32_t b[2] = { s, F24_DM_S_LEN };
    FIO_WriteFile(f, b, sizeof(b));
    FIO_WriteFile(f, (void *) s, F24_DM_S_LEN);     /* FIO_WriteFile flushes and uses the uncached alias */
    for (int k = 0; k < 3; k++)
    {
        if (!h[k]) continue;
        b[0] = h[k]; b[1] = n[k];
        FIO_WriteFile(f, b, sizeof(b));
        FIO_WriteFile(f, (void *) h[k], n[k]);
    }
    FIO_CloseFile(f);
}

static void f24_dm_step(void)
{
    static int last, file;
    if (!f24_debug || !f24_live() || ml_shutdown_requested) return;
    int now = get_ms_clock();
    if (now - last < 2000) return;
    last = now;
    f24_dm_dump(file ? "ML/LOGS/F24DM1.BIN" : "ML/LOGS/F24DM0.BIN");
    file ^= 1;
}

/* ------------------------------------------------------------------------
 * task
 * ------------------------------------------------------------------------ */

static void f24_hint(void)
{
    int s = f24_stored();
    if (s == 1 && f24_target() && !f24_live())
        NotifyBox(8000, "24.00p ready. Canon's 24.00p row is already Enable:\n"
                        "set it to Disable and back to Enable once\n"
                        "(Movie rec. quality), or change the frame rate and back.");
    else if (s == 0)
        NotifyBox(5000, "24.00p ready: set Canon's 24.00p row to Enable\n"
                        "(Movie rec. quality), with 4K UHD 23.98p.");
}

static void f24_task(void * unused)
{
    TASK_LOOP
    {
        msleep(200);
        if (ml_shutdown_requested) break;
        f24_dm_step();                          /* also while recording / BUSY */
        if (!f24_can_change()) continue;
        f24_p6_step();

        if (f24_ui && !f24_all_in())
        {
            int had_p5 = f24_p5_in();
            if (!f24_apply())
            {
                f24_ui = 0;
                NotifyBox(5000, "24.00p refused:\n%s", f24_why);
            }
            else if (!had_p5) f24_hint();
        }
        else if (!f24_ui && f24_p5_in())
        {
            /* the builder first: the next rebuild of Canon's movie parameters is 23.98p */
            f24_remove(F24_G_P5, F24_G_P5);
            if (f24_live())
                NotifyBox(8000, "24.00p OFF. To get 23.98p back now:\n"
                                "set Canon's 24.00p row to Disable,\n"
                                "or change the frame rate once and back.");
        }
        else if (!f24_ui && f24_row_in() && !f24_live())
        {
            /* Canon rebuilt its parameters without 24.00: the row can go */
            f24_remove(F24_G_P5 + 1, F24_NG - 1);
        }
    }
}

/* ------------------------------------------------------------------------
 * menu
 * ------------------------------------------------------------------------ */

static MENU_UPDATE_FUNC(f24_update)
{
    int s = f24_stored();
    if (s >= 0) MENU_SET_RINFO("%s%s", s ? "Enable" : "Disable", f24_live() ? ", 24.00" : "");

    if (!f24_ui && f24_p5_in())
        MENU_SET_WARNING(MENU_WARN_INFO, "Turns off in LiveView (not in playback or while recording).");
    else if (!f24_ui && f24_row_in())
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Still 24.00p: set Canon's 24.00p row to Disable, or change the frame rate once.");
    else if (!f24_ui && f24_why[0])
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Refused: %s", f24_why);
    else if (f24_ui && !f24_all_in())
        MENU_SET_WARNING(MENU_WARN_INFO, "Takes effect in LiveView (not in playback or while recording).");
    else if (f24_ui && f24_live())
        MENU_SET_WARNING(MENU_WARN_INFO, "24.00p live in 4K UHD (fps index %d, VD %d). FHD stays 23.98p.",
                         f24_rd(F24_TIMING_INFO + 0x24), f24_rd(F24_IMGCTRL + 0x10));
    else if (f24_ui && s != 1)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Set Canon's 24.00p row (Movie rec. quality) to Enable.");
    else if (f24_ui && f24_target())
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Not live yet: set Canon's 24.00p row to Disable and back to Enable once.");
    else if (f24_ui)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "Acts on 4K UHD 23.98p (NTSC) only; FHD stays 23.98p.");
}

static struct menu_entry f24_menu[] =
{
    {
        .name   = "24.00p (Canon row)",
        .priv   = &f24_ui,
        .max    = 1,
        .update = f24_update,
        .help   = "Canon's 24.00p row in Movie rec. quality; Enable records 4K UHD 23.98p as 24.00p.",
        .help2  = "4K 23.98p (NTSC) only, FHD stays 23.98p. Untested sensor sync. Not with open gate.",
        .children = (struct menu_entry[]) {
            {
                .name   = "Debug dump",
                .priv   = &f24_debug,
                .max    = 1,
                .help   = "While 24.00p is live: Canon's debug ring to ML/LOGS/F24DM0/1.BIN every 2 s.",
            },
            MENU_EOL,
        },
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
    /* at power off touch nothing: the patches and cells are in RAM, stock at the next boot */
    if (ml_shutdown_requested) return 0;
    if (f24_can_change())
    {
        f24_remove(0, F24_NG - 1);              /* the builder first */
        if (f24_p6_in) { unpatch_memory(f24_p6[0].addr); f24_p6_in = 0; }
    }
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(f24_init)
    MODULE_DEINIT(f24_deinit)
MODULE_INFO_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(f24_ui)
    MODULE_CONFIG(f24_debug)
MODULE_CONFIGS_END()
