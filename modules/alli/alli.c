/** \file
 * alli for the R6 1.9.0: Canon's own ALL-I ("For editing") in Movie rec. quality, up to 30p
 * (research/all-i-feasibility.md section 7).
 *
 * Canon's Movie rec. quality dialog lays out its choices from ROM cell lists, one per
 * video system / movie crop / HFR state, and the R6's lists have no ALL-I cells. The rest
 * is all there: the size kinds, the H.264 and HEVC (Canon Log / HDR PQ) ALL-I encoder rows,
 * the "For editing (ALL-I)" caption in every language, and the recording chain that
 * time-lapse already uses with the same ALL-I parameters. ON points the 4 normal lists to
 * copies in RAM with an ALL-I cell before each IPB cell up to 30p, in both the 8-bit and
 * the 10-bit variant. Choosing it is Canon's own path (SetMovieSizeKind), so Canon stores
 * the setting and restores it at boot. Canon's player limit (470.2 Mbps) is raised too:
 * 4K ALL-I is 470 Mbps of video plus audio.
 *
 * Apply: check every stock word, the selector literal and each stock list cell by cell,
 * then one apply_patches() call (all or nothing; it checks the stock bytes again).
 * OFF, a refused apply, or a boot with the entry OFF while an ALL-I setting is stored:
 * Canon's SetMovieSizeKind with the IPB kind of the same size, rate and crop family,
 * then the patches go. Everything happens in LiveView only (no Canon dialog, no ML menu
 * open, not recording); nothing at power off.
 *
 * Values and their sources: alli_r6_190.h.
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <menu.h>
#include <patch.h>
#include <config.h>

#include "alli_r6_190.h"

/* src/patch_mmu.c (not declared in patch.h for modules) */
extern int mmu_reserve_pages_for_patches(const struct patch * patches, uint32_t count);

/* ML/LOGS/ALLI.LOG: one line per apply / removal / restore (task context only, rare) */
static void ai_log(const char * fmt, ...)
{
    if (ml_shutdown_requested) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(buf, sizeof(buf), "%d ", get_ms_clock());
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    FILE * f = FIO_CreateFileOrAppend("ML/LOGS/ALLI.LOG");
    if (!f) return;
    FIO_WriteFile(f, buf, strlen(buf));
    FIO_WriteFile(f, "\n", 1);
    FIO_CloseFile(f);
}

#define RD32(a) (*(volatile uint32_t *)(a))

static CONFIG_INT("alli.on", ai_ui, 0);         /* menu, saved */
static CONFIG_INT("alli.debug", ai_debug, 0);   /* REC log, saved */
static const char * ai_why = "";                /* why the last apply / restore was refused */
static int ai_in;                               /* patches applied */
static int ai_restore_refused;                  /* Canon refused IPB with the entry OFF */

/* Canon's GUI calls (alli_r6_190.h) */
static int (*const ai_get_kind)(uint32_t * kind) = (void *) AI_GET_KIND;
static int (*const ai_set_kind)(uint32_t kind) = (void *) AI_SET_KIND;
static void (*const ai_get_param)(uint8_t rec[16]) = (void *) AI_GET_PARAM;

/* the stored size kind, or AI_KIND_END if unknown */
static uint32_t ai_kind(void)
{
    uint32_t k = AI_KIND_END;
    if (ai_get_kind(&k) != 1) return AI_KIND_END;
    return k;
}

/* the IPB kind of the same size, rate and crop family, if k is one of our ALL-I kinds */
static uint32_t ai_ipb_of(uint32_t k)
{
    for (int i = 0; i < COUNT(ai_pairs); i++)
        if (ai_pairs[i].alli == k) return ai_pairs[i].ipb;
    return 0;
}

static uint32_t ai_alli_of(uint32_t k)
{
    for (int i = 0; i < COUNT(ai_pairs); i++)
        if (ai_pairs[i].ipb == k) return ai_pairs[i].alli;
    return 0;
}

static int ai_opengate_in(void)
{
    return RD32(AI_OPENGATE_WORD) != AI_OPENGATE_STOCK;
}

/* LiveView, no playback, no recording: no Canon menu dialog is open */
static int ai_can_change(void)
{
    return lv && !is_play_or_qr_mode() && !RECORDING;
}

/* for Canon's setter also no ML menu on screen */
static int ai_can_restore(void)
{
    return ai_can_change() && !gui_menu_shown();
}

/* ------------------------------------------------------------------------
 * cell lists in RAM
 * ------------------------------------------------------------------------ */

#define AI_NL COUNT(ai_layouts)
#define AI_VWORDS (AI_LAYOUT_SIZE / 4)
static uint32_t ai_ram[AI_NL][AI_LAYOUT_VARIANTS * AI_VWORDS];

/* checks one stock variant (type, cells, end, zeros after it) and writes ours. Returns 0
 * with ai_why set if anything is unexpected */
static int ai_build_variant(const struct ai_layout * L, int v, uint32_t * dst)
{
    const uint32_t * src = (const uint32_t *) (L->stock + v * AI_LAYOUT_SIZE);
    const struct ai_cell * sc = (const struct ai_cell *) (src + 1);
    struct ai_cell * dc = (struct ai_cell *) (dst + 1);
    int n = L->n[v];

    if (src[0] != AI_LAYOUT_TYPE) goto bad;
    for (int c = 0; c < n; c++)
    {
        if (sc[c].kind != L->kinds[v][c] || sc[c].proxy != AI_KIND_END || sc[c].zero != 0
            || (sc[c].caption != AI_CAP_IPB && sc[c].caption != AI_CAP_LIGHT))
        {
            printf("alli: %s/%d cell %d: kind %d caption %x\n", L->what, v, c, sc[c].kind, sc[c].caption);
            goto bad;
        }
    }
    if (sc[n].kind != AI_KIND_END || sc[n].proxy != AI_KIND_END) goto bad;
    for (uint32_t * w = (uint32_t *) &sc[n].size_icon; w < (uint32_t *) (src + AI_VWORDS); w++)
        if (*w) goto bad;

    memset(dst, 0, AI_LAYOUT_SIZE);
    dst[0] = src[0];
    int m = 0;
    for (int c = 0; c < n; c++)
    {
        uint32_t a = sc[c].caption == AI_CAP_IPB ? ai_alli_of(sc[c].kind) : 0;
        if (a)
        {
            if (m >= AI_MAX_CELLS - 1) goto bad;
            dc[m] = sc[c];
            dc[m].kind = a;
            dc[m].caption = AI_CAP_ALLI;
            m++;
        }
        if (m >= AI_MAX_CELLS - 1) goto bad;
        dc[m++] = sc[c];
    }
    dc[m].kind = AI_KIND_END;
    dc[m].proxy = AI_KIND_END;
    return 1;

bad:
    ai_why = "Canon's list not stock (other firmware?)";
    return 0;
}

/* ------------------------------------------------------------------------
 * ROM patches
 * ------------------------------------------------------------------------ */

/* the 4 selector pointers lie 0x1C apart: one patch over them (the key words between them
 * are written back unchanged) */
#define AI_SEL_LEN sizeof(ai_sel_stock)
static uint32_t ai_sel_buf[2][COUNT(ai_sel_stock)];

static int ai_apply(void)
{
    if (ai_opengate_in()) { ai_why = "Turn the open gate feature OFF first"; return 0; }

    int stock = RD32(AI_SEL_LITERAL) == AI_SEL_TABLE
             && RD32(AI_ICON_TABLE + 8 * AI_ICON_ALLI_PAIR) == AI_CAP_ALLI
             && RD32(AI_PLAYER_BITRATE) == AI_PLAYER_BITRATE_STOCK;
    for (int i = 0; i < COUNT(ai_sel_stock); i++)
        if (RD32(AI_SEL_FIRST + 4 * i) != ai_sel_stock[i])
        {
            printf("alli: %08x is %08x, not %08x\n", AI_SEL_FIRST + 4 * i, RD32(AI_SEL_FIRST + 4 * i), ai_sel_stock[i]);
            stock = 0;
        }
    if (!stock)
    {
        printf("alli: literal %08x, icon %x, player %d\n", RD32(AI_SEL_LITERAL),
               RD32(AI_ICON_TABLE + 8 * AI_ICON_ALLI_PAIR), RD32(AI_PLAYER_BITRATE));
        ai_why = "ROM not stock (other firmware or patch?)";
        return 0;
    }

    memcpy(ai_sel_buf[0], ai_sel_stock, AI_SEL_LEN);
    memcpy(ai_sel_buf[1], ai_sel_stock, AI_SEL_LEN);
    for (int l = 0; l < AI_NL; l++)
    {
        const struct ai_layout * L = &ai_layouts[l];
        uint32_t i = (L->sel_word - AI_SEL_FIRST) / 4;
        if (L->sel_word < AI_SEL_FIRST || i >= COUNT(ai_sel_stock) || ai_sel_stock[i] != L->stock)
        {
            ai_why = "internal: selector table";
            return 0;
        }
        for (int v = 0; v < AI_LAYOUT_VARIANTS; v++)
            if (!ai_build_variant(L, v, &ai_ram[l][v * AI_VWORDS])) return 0;
        ai_sel_buf[1][i] = (uint32_t) ai_ram[l];
    }

    struct patch p[2] =
    {
        { .addr = (uint8_t *) AI_SEL_FIRST, .old_values = (uint8_t *) ai_sel_buf[0],
          .new_values = (uint8_t *) ai_sel_buf[1], .size = AI_SEL_LEN, .description = "alli: ALL-I cells" },
        { .addr = (uint8_t *) AI_PLAYER_BITRATE, .old_value = AI_PLAYER_BITRATE_STOCK,
          .new_value = AI_PLAYER_BITRATE_NEW, .size = 4, .description = "alli: player bit rate" },
    };
    int r = mmu_reserve_pages_for_patches(p, COUNT(p));
    if (r)
    {
        ai_log("mmu_reserve_pages_for_patches = %d", r);
        ai_why = "Not enough memory for the patches";
        return 0;
    }
    int e = apply_patches(p, COUNT(p));         /* copies the buffers; all or nothing */
    ai_log("apply: %x", e);
    if (e)
    {
        ai_why = e & E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active"
               : e & E_PATCH_BAD_MMU_PAGE ? "No MMU page left"
               : e & E_PATCH_OLD_VALUE_MISMATCH ? "ROM not stock (other firmware or patch?)"
               : "ROM patch refused";
        return 0;
    }
    ai_in = 1;
    ai_why = "";
    return 1;
}

static void ai_remove(void)
{
    if (!ai_in) return;
    int e1 = unpatch_memory(AI_SEL_FIRST);
    int e2 = unpatch_memory(AI_PLAYER_BITRATE);
    ai_log("remove: %x %x", e1, e2);
    ai_in = 0;
}

/* an ALL-I setting is stored: Canon's own SET, with the IPB kind of the same size and rate.
 * Returns 1 if nothing (more) to do, 0 to try again later, -1 if Canon refused */
static int ai_restore(void)
{
    uint32_t k = ai_kind(), ipb = ai_ipb_of(k);
    if (!ipb) return 1;
    if (!ai_can_restore()) return 0;
    int r = ai_set_kind(ipb);
    uint32_t now = ai_kind();
    ai_log("restore: kind %d -> %d: %d, now %d", k, ipb, r, now);
    if (r != 1 || now != ipb)
    {
        ai_why = "Canon refused the IPB setting";
        return -1;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * debug log (menu "Debug log", off by default): at each REC start and stop, the stored
 * kind and Canon's movie parameter {rate, capture rate, size, GOP, container, quality}
 * ------------------------------------------------------------------------ */

static void ai_rec_log(void)
{
    static int was;
    int now = RECORDING;
    if (now == was) return;
    was = now;
    if (!ai_debug) return;
    uint8_t rec[16] = { 0 };
    ai_get_param(rec);
    uint32_t rate, cap;
    memcpy(&rate, rec, 4);
    memcpy(&cap, rec + 4, 4);
    ai_log("REC %s: kind %d, rate %d/%d, size %d, GOP %d (1 = ALL-I), container %d, quality %d, fps %d",
           now ? "start" : "stop", ai_kind(), rate, cap, rec[8], rec[9], rec[10], rec[11], video_mode_fps_x100);
}

/* ------------------------------------------------------------------------
 * task
 * ------------------------------------------------------------------------ */

static void ai_task(void * unused)
{
    TASK_LOOP
    {
        msleep(200);
        if (ml_shutdown_requested) break;
        ai_rec_log();
        if (!ai_can_change()) continue;

        if (ai_ui) ai_restore_refused = 0;     /* try again after the next OFF */

        if (ai_ui && ai_opengate_in())
        {
            ai_ui = 0;
            NotifyBox(5000, "ALL-I OFF: open gate is on.");
        }

        if (ai_ui && !ai_in)
        {
            if (!ai_apply())
            {
                ai_ui = 0;
                NotifyBox(5000, "ALL-I refused:\n%s", ai_why);
            }
            else NotifyBox(5000, "ALL-I ready: Movie rec. quality,\n"
                                 "\"For editing (ALL-I)\", up to 30p.");
        }
        else if (!ai_ui && !ai_restore_refused)
        {
            /* an ALL-I setting goes back to IPB first, then the cells; also at boot with
             * the entry OFF */
            int r = ai_restore();
            if (r == 1) ai_remove();
            else if (r < 0 && ai_in)
            {
                /* the ALL-I cell stays, so Canon's menu still shows what is set */
                ai_ui = 1;
                NotifyBox(8000, "ALL-I stays ON: Canon refused IPB.\n"
                                "Choose an IPB setting in Movie rec.\n"
                                "quality, then turn ALL-I OFF.");
            }
            else if (r < 0)
            {
                ai_restore_refused = 1;     /* once per boot */
                NotifyBox(8000, "ALL-I is set: Canon refused IPB.\n"
                                "Choose an IPB setting in\n"
                                "Movie rec. quality.");
            }
        }
    }
}

/* ------------------------------------------------------------------------
 * menu
 * ------------------------------------------------------------------------ */

static MENU_UPDATE_FUNC(ai_update)
{
    int stored = ai_ipb_of(ai_kind()) != 0;
    if (stored) MENU_SET_RINFO("ALL-I set");

    if (!ai_ui && (ai_in || stored))
        MENU_SET_WARNING(MENU_WARN_INFO, "Back to IPB, then off: in LiveView, ML menu closed.");
    else if (!ai_ui && ai_why[0])
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Refused: %s", ai_why);
    else if (ai_ui && !ai_in)
        MENU_SET_WARNING(MENU_WARN_INFO, "Applies in LiveView, outside playback and REC.");
    else if (ai_ui)
        MENU_SET_WARNING(MENU_WARN_ADVICE, "4K ALL-I is 470 Mbps: use a V90 card.");
}

static struct menu_entry ai_menu[] =
{
    {
        .name   = "ALL-I",
        .priv   = &ai_ui,
        .max    = 1,
        .update = ai_update,
        .help   = "Canon's ALL-I (For editing) in Movie rec. quality, up to 30p.",
        .help2  = "4K ALL-I is 470 Mbps: use a V90 card.",
        .children = (struct menu_entry[]) {
            {
                .name   = "Debug log",
                .priv   = &ai_debug,
                .max    = 1,
                .help   = "Movie setting at each REC start / stop, ML/LOGS/ALLI.LOG.",
            },
            MENU_EOL,
        },
    },
};

/* ------------------------------------------------------------------------
 * module
 * ------------------------------------------------------------------------ */

static unsigned int ai_init(void)
{
    if (!is_camera("R6", "*"))
    {
        printf("alli: R6 only\n");
        return CBR_RET_ERROR;
    }
    menu_add("Movie", ai_menu, COUNT(ai_menu));
    task_create("alli_task", 0x1d, 0x1000, ai_task, 0);
    return 0;
}

static unsigned int ai_deinit(void)
{
    /* at power off touch nothing: the patches are in RAM, stock at the next boot; a stored
     * ALL-I setting goes back to IPB when the module runs with the entry OFF */
    if (ml_shutdown_requested) return 0;
    if (ai_can_restore() && ai_restore() == 1) ai_remove();
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(ai_init)
    MODULE_DEINIT(ai_deinit)
MODULE_INFO_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(ai_ui)
    MODULE_CONFIG(ai_debug)
MODULE_CONFIGS_END()
