/** \file
 * Open gate 4K for the R6 1.9.0: the whole sensor height in 4K, recorded at
 * 4096x2736 (3:2), HEVC 4:2:2 10-bit, level 6.1, at 25p, 29.97p or 23.98p.
 *
 * How it works: every change is either
 *  - a ROM word (code or data) patched through apply_patches(), several words of one
 *    page merged into one patch (src/patch.h), or
 *  - a RAM word (a Canon pointer redirected to a module-owned copy of a Canon table,
 *    or a cell of a live Canon buffer), written by the module after a stock check.
 * og_prepare() builds the copies and the full list of changes and checks every stock
 * value without writing anything to Canon memory; og_commit() applies the ROM patches
 * in one all-or-nothing call and then the RAM words; og_remove() is the only undo.
 *
 * Changes are applied only at a safe point: photo LiveView, where path 3 (4K movie)
 * is not running and the MemoryNavigator 4K maps are not active. The movie setting is
 * learnt from Canon's own 4K <= 30p LiveView (capture mode 0x1B, sensor slot 10), so
 * the user enters movie mode once before the first apply.
 *
 * Values and their sources: og_r6_190.h.
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <propvalues.h>
#include <menu.h>
#include <patch.h>
#include <bmp.h>
#include <config.h>

#include "og_r6_190.h"

/* src/patch_mmu.c (not declared in patch.h for modules) */
extern int mmu_reserve_runtime_pages(uint32_t total_pages, uint32_t total_L2_tables);

#define RD32(a) (*(volatile uint32_t *)(a))
#define RD16(a) (*(volatile uint16_t *)(a))

/* ------------------------------------------------------------------------
 * state
 * ------------------------------------------------------------------------ */

enum og_state { OG_OFF, OG_ARMED, OG_APPLIED, OG_ACTIVE, OG_REMOVING };

static int og_ui;                       /* menu: the user's choice (not saved: OFF at boot) */
static CONFIG_INT("opengate.debug", og_debug, 0);   /* menu: write ML/LOGS/OPENGATE.LOG */
static volatile int og_state = OG_OFF;
static const char * og_reason = "";     /* why the last apply was refused or removed */

/* the movie setting, learnt from Canon's stock 4K <= 30p LV */
static volatile int og_setting_col;      /* timing column 2/3/4, 0 = unknown or not OK */
static volatile int og_setting_bad;      /* 1 not 4K <= 30p, 2 crop, 3 fps not supported */
static volatile int og_setting_seq;      /* og_vmode_changes when the setting was learnt */
static volatile int og_hdmi;
static const struct og_rate * og_rate;   /* rate of the applied set */

static struct semaphore * og_lock;      /* worker task vs. property handler removal */

/* ------------------------------------------------------------------------
 * debug log (off by default; one line per event, never from a property handler)
 * ------------------------------------------------------------------------ */

static volatile int og_in_handler;      /* set while a property handler runs: no file access */

static void og_log(const char * fmt, ...)
{
    if (!og_debug || og_in_handler) return;
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(buf, sizeof(buf), "%d ", get_ms_clock());
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    FILE * f = FIO_CreateFileOrAppend("ML/LOGS/OPENGATE.LOG");
    if (f)
    {
        FIO_WriteFile(f, buf, strlen(buf));
        FIO_WriteFile(f, "\n", 1);
        FIO_CloseFile(f);
    }
}

/* ------------------------------------------------------------------------
 * Canon reads (range-checked)
 * ------------------------------------------------------------------------ */

static int og_ram_ok(uint32_t a, uint32_t n)
{
    if (a & 3) return 0;
    if (a >= 0x1000 && a + n < 0x40000000) return 1;
    if (a >= 0x40001000 && a + n < 0x80000000) return 1;
    return 0;
}

static uint32_t og_rd(uint32_t a)
{
    return og_ram_ok(a, 4) ? RD32(a) : 0;
}

static int og_path(void)     { return og_rd(OG_Y2V + 0x64); }
static int og_slot(void)     { return og_rd(OG_IMGCTRL + 8); }
static int og_capmode(void)  { return og_rd(OG_FRAMEDATA + 0x24); }

static int og_in_4k_map(void)
{
    uint32_t m = og_rd(OG_MEMNAV + 8);
    return m == 4 || m == 7;
}

/* a safe point: no 4K movie LV running, no 4K MemoryNavigator map active */
static int og_safe_point(void)
{
    if (RECORDING) return 0;
    if (lv && og_path() != OG_PATH_PHOTO) return 0;
    return !og_in_4k_map();
}

/* ------------------------------------------------------------------------
 * module-owned copies of Canon tables
 * ------------------------------------------------------------------------ */

enum { CP_MAP, CP_SAP, CP_MVRAW, CP_OBI, CP_DEV, CP_POST, CP_CROP, CP_ST8, CP_N };
static uint8_t * og_copy[CP_N];
static const uint32_t og_copy_len[CP_N] =
{
    OG_MAP_LEN, OG_SAP_LEN, OG_MVRAW_HI - OG_MVRAW_LO, OG_OBI_HI - OG_OBI_LO,
    OG_DEV_LEN, OG_POST_LEN, OG_CROP_LEN,
    (OG_ST8_VIR_N + OG_ST8_VOFF_N) * OG_ST8_KNOTS * 4,
};

/* allocated at the first apply and never freed (about 38 KB): Canon may keep a pointer
 * to a copy read during a LiveView setup after the words are restored, and a copy left
 * by a deferred word stays in use until Canon rewrites it. The next apply rebuilds the
 * same content in place, at a safe point. */
static int og_alloc_copies(void)
{
    for (int i = 0; i < CP_N; i++)
    {
        if (!og_copy[i]) og_copy[i] = malloc(og_copy_len[i]);
        if (!og_copy[i]) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------
 * the change list
 * ------------------------------------------------------------------------ */

#define OG_MAX_RAM 96
#define OG_MAX_ROM 64
struct og_ramw { uint32_t addr; uint8_t size, deferred; uint32_t stock, val; };
static struct og_ramw og_ram[OG_MAX_RAM];
static int og_n_ram;
static struct og_word og_rom[OG_MAX_ROM];
static int og_n_rom;
static uint32_t og_rom_patch[OG_MAX_ROM];   /* applied patch addresses, for unpatching */
static int og_n_rom_patch;
static int og_list_error;

static uint32_t og_get(uint32_t addr, int size)
{
    return size == 2 ? RD16(addr) : RD32(addr);
}

static void og_set(uint32_t addr, int size, uint32_t v)
{
    if (size == 2) RD16(addr) = v;
    else RD32(addr) = v;
}

/* deferred = may stay applied until the next safe point (MemoryNavigator entries) */
static void og_add_ram(uint32_t addr, int size, uint32_t stock, uint32_t val, int deferred)
{
    if (og_n_ram >= OG_MAX_RAM) { og_list_error = 1; return; }
    og_ram[og_n_ram++] = (struct og_ramw) { addr, size, deferred, stock, val };
}

static void og_add_rom(uint32_t addr, uint32_t stock, uint32_t val, int insn, const char * what)
{
    if (og_n_rom >= OG_MAX_ROM) { og_list_error = 1; return; }
    og_rom[og_n_rom++] = (struct og_word) { addr, stock, val, insn, what };
}

static void og_add_words(const struct og_word * w, int n)
{
    for (int i = 0; i < n; i++) og_add_rom(w[i].addr, w[i].stock, w[i].val, w[i].insn, w[i].what);
}

/* ROM words in one 64 KB page less than this apart become one multi-byte patch:
 * MAX_PATCHES is 48 on R6 and the 60-min clip option alone takes 15 (RESULTS run 44) */
#define OG_MERGE_GAP 0x100

static int og_apply_rom(void)
{
    int n = og_n_rom;
    int idx[OG_MAX_ROM];
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (og_rom[idx[j]].addr < og_rom[idx[i]].addr) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }

    struct patch p[OG_MAX_ROM];
    uint8_t * bufs[OG_MAX_ROM];
    int np = 0, err = 0;
    for (int i = 0; i < n && !err; )
    {
        uint32_t start = og_rom[idx[i]].addr, end = start + 4;
        int k = i + 1;
        while (k < n && (og_rom[idx[k]].addr & 0xFFFF0000) == (start & 0xFFFF0000)
               && og_rom[idx[k]].addr >= end && og_rom[idx[k]].addr - end < OG_MERGE_GAP)
        {
            end = og_rom[idx[k]].addr + 4;
            k++;
        }
        uint32_t len = end - start;
        bufs[np] = malloc(2 * len);
        if (!bufs[np]) { err = E_PATCH_CANNOT_MALLOC; break; }
        uint8_t * oldb = bufs[np], * newb = bufs[np] + len;
        memcpy(oldb, (void *) start, len);
        memcpy(newb, (void *) start, len);
        int insn = 0;
        for (int m = i; m < k; m++)
        {
            const struct og_word * w = &og_rom[idx[m]];
            uint32_t off = w->addr - start;
            if (*(uint32_t *) (oldb + off) != w->stock)
            {
                og_log("rom %08x (%s) is %08x, not %08x", w->addr, w->what, *(uint32_t *) (oldb + off), w->stock);
                err = E_PATCH_OLD_VALUE_MISMATCH;
            }
            *(uint32_t *) (newb + off) = w->val;
            insn |= w->insn;
        }
        if (len == 4)
            p[np] = (struct patch) { .addr = (uint8_t *) start, .old_value = *(uint32_t *) oldb,
                                     .new_value = *(uint32_t *) newb, .size = 4, .is_instruction = insn,
                                     .description = og_rom[idx[i]].what };
        else
            p[np] = (struct patch) { .addr = (uint8_t *) start, .old_values = oldb, .new_values = newb,
                                     .size = len, .is_instruction = insn, .description = og_rom[idx[i]].what };
        og_rom_patch[np] = start;
        np++;
        i = k;
    }
    if (!err) err = apply_patches(p, np);   /* copies the arrays; all or nothing */
    for (int b = 0; b < np; b++) free(bufs[b]);
    og_log("rom: %d words in %d patches: %x", n, np, err);
    og_n_rom_patch = err ? 0 : np;
    return err;
}

static void og_remove_rom(void)
{
    for (int i = og_n_rom_patch - 1; i >= 0; i--)
    {
        int e = unpatch_memory(og_rom_patch[i]);
        if (e) og_log("unpatch %08x: %x", og_rom_patch[i], e);
    }
    og_n_rom_patch = 0;
}

static int og_check_ram(void)
{
    for (int i = 0; i < og_n_ram; i++)
    {
        uint32_t v = og_get(og_ram[i].addr, og_ram[i].size);
        if (v != og_ram[i].stock)
        {
            og_log("ram %08x is %x, not %x", og_ram[i].addr, v, og_ram[i].stock);
            return 0;
        }
    }
    return 1;
}

/* restore in reverse order; deferred words only when allowed. A word that Canon has
 * rewritten (neither stock nor ours) is Canon's again: logged and left alone. Returns the
 * number of deferred words still to restore */
static int og_restore_ram(int include_deferred)
{
    int left = 0;
    for (int i = og_n_ram - 1; i >= 0; i--)
    {
        struct og_ramw * w = &og_ram[i];
        uint32_t v = og_get(w->addr, w->size);
        if (v == w->stock) continue;
        if (v != w->val)
            og_log("ram %08x is %x (neither stock %x nor ours %x), left to Canon", w->addr, v, w->stock, w->val);
        else if (w->deferred && !include_deferred)
            left++;
        else
            og_set(w->addr, w->size, w->stock);
    }
    return left;
}

/* ------------------------------------------------------------------------
 * develop rules (FRESH-REVIEW.md 3 and 7.2; AUDIT R3/R4). Each rule is first run with
 * Canon's stock inputs and must reproduce the stock record exactly.
 * ------------------------------------------------------------------------ */

#include "og_rules.h"

/* ------------------------------------------------------------------------
 * geometry of the mode (one instance; a native-resolution mode would be another)
 * ------------------------------------------------------------------------ */

struct og_geometry
{
    uint16_t vin, vout;         /* develop input rows (sensor area) and output rows */
    uint16_t b0, b1;            /* develop input bands: b0 = 0 mod 8, b0 + b1 = vin */
    uint16_t stock_vin, stock_vout, stock_b0, stock_b1;
};
static const struct og_geometry og_geo = { 3648, 2736, 2200, 1448, 2886, 2160, 1504, 1382 };

/* ------------------------------------------------------------------------
 * prepare: build every copy and the change list, check every stock value.
 * Writes nothing to Canon memory. Returns 0 or sets og_reason.
 * ------------------------------------------------------------------------ */

#define FAIL(why) do { og_reason = (why); og_log("prepare: %s", why); return 0; } while (0)

static int og_prepare(const struct og_rate * r)
{
    og_n_ram = og_n_rom = 0;
    og_list_error = 0;
    const struct og_geometry * g = &og_geo;

    if (RD32(OG_BLOB_PTRS + 0x18) != OG_BLOB_CMOS || RD32(OG_BLOB_PTRS + 0x1C) != OG_BLOB_DIGIC
        || RD32(OG_TIMING_INFO + 0x20) != OG_TIMING_TABLE)
        FAIL("Canon sensor tables moved");

    /* raw ring: both 4K MemoryNavigator sub-maps (applied only outside them) */
    uint32_t mnav = og_rd(OG_MEMNAV);
    if (!og_ram_ok(mnav, 0x1000)) FAIL("MemoryNavigator not found");
    static const uint32_t subs[2] = { OG_MNAV_SUB_A, OG_MNAV_SUB_B };
    for (int k = 0; k < 2; k++)
    {
        uint32_t e = mnav + subs[k] + OG_MNAV_RAW;
        if (RD32(e) != 5 || RD32(e + 4) != 0) FAIL("MemoryNavigator raw entry unexpected");
        og_add_ram(e + 8, 4, OG_RAW_SLOT_STOCK, OG_RAW_SLOT, 1);
        og_add_ram(e + 12, 4, OG_RAW_N_STOCK, OG_RAW_N, 1);
    }

    /* sensor slot 9 full height + HEAD output */
    for (int i = 0; i < COUNT(og_sensor_cells); i++)
    {
        const struct og_cell * c = &og_sensor_cells[i];
        if (c->stock == c->val)
        {
            if (og_get(c->addr, c->size) != c->stock) FAIL("sensor register list moved");
        }
        else og_add_ram(c->addr, c->size, c->stock, c->val, 0);
    }

    /* timing cells of the rate's column (HD and SetP/cartridge as slot 9) */
    int col = r->col;
    if (RD16(OG_TCELL(0xF00, col)) != OG_T_FLAG) FAIL("timing table unexpected");
    og_add_ram(OG_TCELL(0x000, col), 2, r->fl_stock, r->fl, 0);
    og_add_ram(OG_TCELL(0x280, col), 2, OG_T_SETP_STOCK, OG_T_SETP, 0);
    og_add_ram(OG_TCELL(0x500, col), 2, OG_T_ROD_STOCK, OG_T_ROD, 0);
    og_add_ram(OG_TCELL(0x780, col), 2, r->rps_stock, r->rps, 0);
    og_add_ram(OG_TCELL(0xA00, col), 2, OG_T_HD_STOCK, OG_T_HD, 0);
    og_add_ram(OG_TCELL(0xC80, col), 2, OG_T_HDUS_STOCK, OG_T_HDUS, 0);
    og_add_ram(OG_TCART(col), 4, OG_T_CART_STOCK, OG_T_CART, 0);

    /* capture-mode map: 0x1B -> slot 9, three readers */
    uint8_t * map = og_copy[CP_MAP];
    uint32_t map_tbl = (uint32_t) map + (OG_MAP_TBL - OG_MAP_ROM);
    if (RD32(OG_MAP_TBL + 4 * OG_CAPMODE_4K30) != OG_SLOT_4K30 || RD32(OG_MAP_TBL + 4 * 0x1C) != OG_SLOT_FULL)
        FAIL("capture-mode map unexpected");
    memcpy(map, (void *) OG_MAP_ROM, OG_MAP_LEN);
    *(uint32_t *) (map_tbl + 4 * OG_CAPMODE_4K30) = OG_SLOT_FULL;
    og_add_ram(OG_LVCAP + 0x3C, 4, OG_MAP_TBL, map_tbl, 0);
    og_add_rom(OG_LIT_MAP1, OG_MAP_TBL, map_tbl, 0, "OG map 0x1B -> slot 9 (sensor)");
    og_add_rom(OG_LIT_MAP2, OG_MAP_TBL, map_tbl, 0, "OG map 0x1B -> slot 9 (change)");

    /* SAP frame size of mode 9: 5648x3768 */
    uint8_t * sap = og_copy[CP_SAP];
    uint32_t m9 = OG_SAP_TBL + 16 * OG_SLOT_FULL;
    if (RD32(m9) != 5648 || RD32(m9 + 4) != 3000) FAIL("SAP size table unexpected");
    memcpy(sap, (void *) OG_SAP_TBL, OG_SAP_LEN);
    *(uint32_t *) (sap + (m9 + 4 - OG_SAP_TBL)) = 3768;
    og_add_rom(OG_LIT_SAP, OG_SAP_TBL, (uint32_t) sap, 0, "OG SAP sizes");
    og_add_words(og_sapwin_words, COUNT(og_sapwin_words));

    /* MvRawPro: copy, rebase the 65 internal pointers, record #0 and side records */
    uint8_t * mv = og_copy[CP_MVRAW];
    uint32_t mv_len = OG_MVRAW_HI - OG_MVRAW_LO;
    memcpy(mv, (void *) OG_MVRAW_LO, mv_len);
    int32_t delta = (uint32_t) mv - OG_MVRAW_LO;
    for (uint32_t o = 0; o < mv_len; o += 4)
    {
        uint32_t * w = (uint32_t *) (mv + o);
        if (*w >= OG_MVRAW_LO && *w < OG_MVRAW_HI) *w += delta;
    }
    for (int i = 0; i < COUNT(og_rec_edits); i++)
    {
        uint32_t * w = (uint32_t *) (mv + (og_rec_edits[i].addr - OG_MVRAW_LO));
        if (*w != og_rec_edits[i].stock) FAIL("readout record unexpected");
        *w = og_rec_edits[i].val;
    }
    og_add_rom(OG_LIT_KEYS, OG_MVRAW_LO, (uint32_t) mv, 0, "OG readout records");
    og_add_rom(OG_LIT_SUB, OG_MVRAW_SUB, (uint32_t) mv + (OG_MVRAW_SUB - OG_MVRAW_LO), 0, "OG readout sub");

    /* OB integration, 4K entry */
    uint8_t * obi = og_copy[CP_OBI];
    memcpy(obi, (void *) OG_OBI_LO, OG_OBI_HI - OG_OBI_LO);
    uint32_t * obh = (uint32_t *) (obi + (OG_OBI_H_4K - OG_OBI_LO));
    if (*obh != 2904) FAIL("OB integration table unexpected");
    *obh = 3660;
    og_add_rom(OG_LIT_OBI, OG_OBI_LO, (uint32_t) obi, 0, "OG OB integration sizes");

    /* develop: rules must reproduce the stock record, then bands + scale for the mode */
    uint8_t * dev = og_copy[CP_DEV];
    if (RD32(OG_DEV_PTR) != OG_DEV_REC) FAIL("develop record pointer moved");
    memcpy(dev, (void *) OG_DEV_REC, OG_DEV_LEN);
    if (*(uint32_t *) (dev + 0x28) != g->stock_b0 || *(uint32_t *) (dev + 0x2C) != g->stock_b1
        || *(uint32_t *) (dev + 0x8E0) != g->stock_vout || *(uint32_t *) (dev + 0x8E4) != g->stock_vin)
        FAIL("develop record unexpected");
    uint16_t vf_off[OG_VF_MAX];
    uint32_t vf_val[OG_VF_MAX];
    struct og_scale sst = og_scale_rule(g->stock_vin, g->stock_vout, g->stock_b0);
    for (int pipe = 0; pipe < 2; pipe++)
    {
        int n = og_vfields(g->stock_b0, g->stock_b1, vf_off, vf_val);
        for (int i = 0; i < n; i++)
            if (*(uint32_t *) (dev + OG_DEV_PIPE1 * pipe + vf_off[i]) != vf_val[i]) FAIL("develop rule mismatch (bands)");
        n = og_vdma(g->stock_b0, g->stock_b1, vf_off, vf_val);
        for (int i = 0; i < n; i++)
            if (*(uint32_t *) (dev + OG_DEV_DMA1 * pipe + vf_off[i]) != vf_val[i]) FAIL("develop rule mismatch (DMA)");
        if (!og_scale_matches(dev + OG_DEV_SCALE1 * pipe, &sst)) FAIL("develop rule mismatch (scale)");
    }
    struct og_scale sc = og_scale_rule(g->vin, g->vout, g->b0);
    *(uint32_t *) (dev + 0x28) = g->b0;
    *(uint32_t *) (dev + 0x2C) = g->b1;
    *(uint32_t *) (dev + 0x8E0) = g->vout;
    *(uint32_t *) (dev + 0x8E4) = g->vin;
    for (int pipe = 0; pipe < 2; pipe++)
    {
        int n = og_vfields(g->b0, g->b1, vf_off, vf_val);
        for (int i = 0; i < n; i++) *(uint32_t *) (dev + OG_DEV_PIPE1 * pipe + vf_off[i]) = vf_val[i];
        n = og_vdma(g->b0, g->b1, vf_off, vf_val);
        for (int i = 0; i < n; i++) *(uint32_t *) (dev + OG_DEV_DMA1 * pipe + vf_off[i]) = vf_val[i];
        og_scale_write(dev + OG_DEV_SCALE1 * pipe, &sc);
    }
    og_add_ram(OG_DEV_PTR, 4, OG_DEV_REC, (uint32_t) dev, 0);

    /* MvPostDevelop: frame Vout, tiles Vout/2, ratio Vout:Vin, display crop centred */
    uint8_t * post = og_copy[CP_POST], * crop = og_copy[CP_CROP];
    if (RD32(OG_POST_PTR) != OG_POST_REC) FAIL("post-develop pointer moved");
    memcpy(post, (void *) OG_POST_REC, OG_POST_LEN);
    memcpy(crop, (void *) OG_CROP_TBL, OG_CROP_LEN);
    if (*(uint32_t *) (post + 0x38) != g->stock_vout / 2 || *(uint32_t *) (post + 0x40) != g->stock_vout / 2
        || *(uint32_t *) (post + 0x5C) != g->stock_vout || *(uint32_t *) (post + 0x68) != g->stock_vout - 1u
        || *(uint32_t *) (post + 0xAC) != OG_CROP_TBL
        || *(uint32_t *) (crop + 0x04) != 0 || *(uint32_t *) (crop + 0x2C) != 0
        || *(uint32_t *) (crop + 0x0C) != g->stock_vout || *(uint32_t *) (crop + 0x34) != g->stock_vout)
        FAIL("post-develop record unexpected");
    uint32_t gcd = og_gcd(g->vout, g->vin);
    *(uint32_t *) (post + 0x38) = g->vout / 2;
    *(uint32_t *) (post + 0x40) = g->vout / 2;
    *(uint32_t *) (post + 0x5C) = g->vout;
    *(uint32_t *) (post + 0x68) = g->vout - 1u;
    *(uint32_t *) (post + 0xC0) = g->vout / gcd;
    *(uint32_t *) (post + 0xC8) = g->vin / gcd;
    *(uint32_t *) (post + 0xAC) = (uint32_t) crop;
    *(uint32_t *) (crop + 0x04) = (g->vout - g->stock_vout) / 2;
    *(uint32_t *) (crop + 0x2C) = (g->vout - g->stock_vout) / 2;
    og_add_ram(OG_POST_PTR, 4, OG_POST_REC, (uint32_t) post, 0);
    og_add_words(og_memmap_words, COUNT(og_memmap_words));

    /* SAP stage 8 per-row tables: 120-knot copies of the 26 VIR and 4 V offset tables */
    uint32_t vir = og_rd(OG_ST8_VIR_BASE), voff = og_rd(OG_ST8_VOFF_BASE);
    if (!og_ram_ok(vir, 0x200) || !og_ram_ok(voff, 0x800)) FAIL("stage 8 tables not found");
    uint32_t * tabs = (uint32_t *) og_copy[CP_ST8];
    for (int i = 0; i < OG_ST8_VIR_N + OG_ST8_VOFF_N; i++)
    {
        uint32_t slot = i < OG_ST8_VIR_N ? vir + 0x148 + 4 * i : voff + 0x780 + 4 * (i - OG_ST8_VIR_N);
        uint32_t src = RD32(slot);
        if (!og_ram_ok(src, OG_ST8_KNOTS_STOCK * 4)) FAIL("stage 8 table unexpected");
        uint32_t * t = tabs + i * OG_ST8_KNOTS;
        for (int j = 0; j < OG_ST8_KNOTS; j++)
            t[j] = RD32(src + 4 * (j < OG_ST8_KNOTS_STOCK ? j : OG_ST8_KNOTS_STOCK - 1));
        /* deferred: stage 8 reads 120 words per frame (count set at SetPath) until the
         * next setup; words 0-95 of the copies are stock, so a copy is safe in any mode */
        og_add_ram(slot, 4, src, (uint32_t) t, 1);
    }
    og_add_words(og_knot_words, COUNT(og_knot_words));

    og_add_words(og_disp_words, COUNT(og_disp_words));
    og_add_words(og_enc_words, COUNT(og_enc_words));

    if (og_list_error) FAIL("internal: change list full");
    if (!og_check_ram()) FAIL("a Canon value is not stock");
    og_log("prepare %s: %d RAM words, %d ROM words", r->name, og_n_ram, og_n_rom);
    return 1;
}

#undef FAIL

/* ------------------------------------------------------------------------
 * commit / remove (worker task, or the property handler under og_lock)
 * ------------------------------------------------------------------------ */

static int og_pages_ok;

static int og_reserve_pages(void)
{
    if (og_pages_ok) return 1;
    for (int i = 0; i < COUNT(og_reserve_steps); i++)
    {
        int e = mmu_reserve_runtime_pages(og_reserve_steps[i][0], og_reserve_steps[i][1]);
        og_log("mmu_reserve_runtime_pages(%d, %d) = %d", og_reserve_steps[i][0], og_reserve_steps[i][1], e);
        if (e) return 0;
    }
    og_pages_ok = 1;
    return 1;
}

static int og_commit(void)
{
    if (RECORDING) { og_reason = "Stop recording first"; return 0; }
    if (!og_reserve_pages()) { og_reason = "Not enough memory for the patches"; return 0; }
    int e = og_apply_rom();
    if (e) { og_reason = e == E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active" : "ROM patch refused"; return 0; }
    if (!og_check_ram())     /* re-check right before writing */
    {
        og_remove_rom();
        og_reason = "A Canon value changed";
        return 0;
    }
    for (int i = 0; i < og_n_ram; i++) og_set(og_ram[i].addr, og_ram[i].size, og_ram[i].val);
    og_log("commit: done");
    return 1;
}

/* outside a safe point the deferred words (MemoryNavigator raw entries, stage-8 table
 * slots) stay until the next one: they are harmless alone */
static int og_pending_deferred;

static void og_remove(void)
{
    int safe = og_safe_point();
    og_remove_rom();
    int left = og_restore_ram(safe);
    og_pending_deferred = left > 0;
    og_log("remove: %s, %d words left", safe ? "safe point" : "LV running", left);
}

static void og_finish_deferred(void)
{
    if (!og_pending_deferred || !og_safe_point()) return;
    int left = og_restore_ram(1);
    og_pending_deferred = left > 0;
    og_log("deferred restore: %d words left", left);
}

/* ------------------------------------------------------------------------
 * property handlers: only set flags; removal on a setting change is synchronous,
 * so it finishes before Canon starts the new movie mode (DESIGN.md 3.3)
 * ------------------------------------------------------------------------ */

static int og_removal_needed(void)
{
    return og_state == OG_APPLIED || og_state == OG_ACTIVE || og_state == OG_REMOVING;
}

/* a lost setting is always recorded here first; whoever holds og_lock next (the handler
 * below, or the worker if the handler could not take the lock or a recording runs)
 * removes the patches. So a change that arrives while the worker is applying is never
 * missed: the worker finishes the apply, gives the lock, and the removal follows. */
static volatile int og_lost_pending;
static const char * volatile og_lost_why = "";
static volatile int og_removed_notice;  /* removal to show to the user (worker) */

/* the caller holds og_lock */
static void og_handle_lost_locked(void)
{
    if (!og_lost_pending || RECORDING) return;
    og_lost_pending = 0;
    og_setting_col = 0;
    if (!og_removal_needed()) return;
    og_remove();
    og_reason = og_lost_why;
    og_state = og_ui ? OG_ARMED : OG_OFF;
    og_removed_notice = 1;
}

/* from a property handler (task context): remove at once if possible */
static void og_setting_lost(const char * why)
{
    og_setting_col = 0;
    og_lost_why = why;
    og_lost_pending = 1;
    if (RECORDING) return;                          /* the worker removes after the stop */
    if (take_semaphore(og_lock, 1000) != 0) return; /* the worker removes on its next step */
    og_handle_lost_locked();
    give_semaphore(og_lock);
}

/* raw words; 0..2 = crop, resolution, fps x 100 on DIGIC X (src/propvalues.c). Only
 * these three are compared: the meaning of the others is not known on R6 */
#define OG_VMODE_N 4
#define OG_VMODE_CMP 3
static uint32_t og_vmode[OG_VMODE_N];
static volatile int og_vmode_changes;
static volatile int og_vmode_calls, og_vmode_len;   /* debug: is the property delivered on R6? */

PROP_HANDLER(PROP_VIDEO_MODE)
{
    og_in_handler = 1;
    og_vmode_calls++;
    og_vmode_len = len;
    int n = MIN(len / 4, OG_VMODE_N);
    int changed = 0;
    for (int i = 0; i < n; i++)
    {
        if (og_vmode[i] != buf[i] && i < OG_VMODE_CMP) changed = 1;
        og_vmode[i] = buf[i];
    }
    if (changed)
    {
        og_vmode_changes++;
        og_setting_lost("Movie size or fps changed");
    }
    og_in_handler = 0;
}

PROP_HANDLER(PROP_HDMI_CHANGE)
{
    og_in_handler = 1;
    og_hdmi = buf[0];
    if (og_hdmi) og_setting_lost("HDMI connected");
    og_in_handler = 0;
}

/* ------------------------------------------------------------------------
 * worker task: the only place that applies; checks the live state
 * ------------------------------------------------------------------------ */

static const struct og_rate * og_rate_of_col(int col)
{
    for (int i = 0; i < COUNT(og_rates); i++)
        if (og_rates[i].col == col) return &og_rates[i];
    return 0;
}

/* the Canon fps setting must be the rate of the column: column 4 is also 24.00p and
 * column 2 may be 30.00p. Returns 1 if OK; if buf[2] does not look like fps x 100 the
 * layout is not the expected one and only the live checks remain (logged) */
static int og_fps_ok(int col)
{
    uint32_t f = og_vmode[2];
    if (f < 1000 || f > 12000)
    {
        static int logged;
        if (!logged) { logged = 1; og_log("PROP_VIDEO_MODE[2] = %d is not fps x 100: fps not checked", f); }
        return 1;
    }
    const struct og_rate * r = og_rate_of_col(col);
    return r && f == r->fps100;
}

/* the learnt setting, saved for the next boot with the PROP_VIDEO_MODE words seen with it */
static CONFIG_INT("opengate.col", og_saved_col, 0);
static CONFIG_INT("opengate.vm0", og_saved_vm0, 0);
static CONFIG_INT("opengate.vm1", og_saved_vm1, 0);
static CONFIG_INT("opengate.vm2", og_saved_vm2, 0);
static CONFIG_INT("opengate.vm.ok", og_saved_vm_ok, 0);

static void og_save_setting(int col)
{
    og_saved_col = col;
    og_saved_vm_ok = col && og_vmode_calls > 0;
    og_saved_vm0 = og_vmode[0];
    og_saved_vm1 = og_vmode[1];
    og_saved_vm2 = og_vmode[2];
}

/* learn the setting from Canon's own (unpatched) movie LV. A sample counts only when the
 * same result is seen OG_STABLE times in a row: while the movie LV starts or stops Canon
 * shows capture mode 0 and stale slots (log of the first clean-module test) */
#define OG_STABLE 5
static void og_observe_setting(void)
{
    static int cand_bad = -1, cand_col, cand_n;
    if (!lv || og_path() != OG_PATH_MOVIE || og_removal_needed()) { cand_n = 0; return; }
    int seq = og_vmode_changes;
    int cap = og_capmode(), slot = og_slot(), col = og_rd(OG_TIMING_INFO + 0x24);
    if (cap == 0) { cand_n = 0; return; }   /* no capture mode: LV starting or stopping */
    int bad = 0;
    if (cap == OG_CAPMODE_4KCROP) bad = 2;
    else if (cap != OG_CAPMODE_4K30 || slot != OG_SLOT_4K30 || !og_rate_of_col(col)) bad = 1;
    else if (!og_fps_ok(col)) bad = 3;
    if (seq != og_vmode_changes) { cand_n = 0; return; }   /* the setting changed while reading */
    if (bad != cand_bad || col != cand_col) { cand_bad = bad; cand_col = col; cand_n = 0; }
    if (++cand_n < OG_STABLE) return;
    cand_n = OG_STABLE;
    if (!bad)
    {
        if (og_setting_col != col) og_log("setting: 4K %s", og_rate_of_col(col)->name);
        og_setting_col = col;
        og_setting_seq = seq;
        og_save_setting(col);
    }
    else
    {
        if (og_setting_bad != bad)
            og_log("setting not usable (%d): capmode %x slot %d fps idx %d fps %d", bad, cap, slot, col, og_vmode[2]);
        og_setting_col = 0;
        og_save_setting(0);
    }
    og_setting_bad = bad;
}

/* after a boot in photo mode: reuse the setting of the last session, only if Canon's
 * PROP_VIDEO_MODE words (delivered at boot) are the ones seen with it. Movie settings
 * change only in movie mode, where the module observes them */
static void og_recall_setting(void)
{
    static int done;
    if (done || !og_vmode_calls) return;   /* once, after the boot delivery of the property */
    done = 1;
    if (og_setting_col) return;
    const struct og_rate * r = og_rate_of_col(og_saved_col);
    if (!r) return;
    int same = og_saved_vm_ok && og_saved_vm0 == (int) og_vmode[0]
            && og_saved_vm1 == (int) og_vmode[1] && og_saved_vm2 == (int) og_vmode[2];
    og_log("last session: 4K %s, PROP_VIDEO_MODE %s", r->name, same ? "same: reused" : "different: not reused");
    if (!same || !og_fps_ok(r->col)) return;
    og_setting_col = r->col;
    og_setting_seq = og_vmode_changes;
}

static int og_active_since;
static int og_bad_samples;

#define OG_BAD_SAMPLES 10   /* 1 s of wrong live state before giving up (Canon re-setups) */

/* movie LV with the patches in: the live state must be the open-gate one. If it is not,
 * the patches are removed at the next safe point and the entry is turned off */
static void og_check_live(void)
{
    if (!lv || og_path() != OG_PATH_MOVIE) { og_active_since = 0; og_bad_samples = 0; return; }
    if (!og_active_since) { og_active_since = get_ms_clock(); return; }
    if (get_ms_clock() - og_active_since < 1500) return;
    int cap = og_capmode(), slot = og_slot(), vd = og_rd(OG_IMGCTRL + 0x10);
    int ok = cap == OG_CAPMODE_4K30 && slot == OG_SLOT_FULL && og_rate
          && vd >= og_rate->fl && vd <= og_rate->fl + 2;
    if (ok)
    {
        if (og_bad_samples) og_log("live state back after %d bad samples", og_bad_samples);
        og_bad_samples = 0;
        if (og_state == OG_APPLIED)
        {
            og_state = OG_ACTIVE;
            og_log("active: capmode %x slot %d VD %d", cap, slot, vd);
            NotifyBox(2000, "Open gate ON: 4096x2736 %s", og_rate->name);
        }
        return;
    }
    if (!og_bad_samples++) og_log("live state wrong: capmode %x slot %d VD %d", cap, slot, vd);
    if (RECORDING) return;
    int give_up = og_state == OG_ACTIVE ? og_bad_samples >= OG_BAD_SAMPLES
                : get_ms_clock() - og_active_since > 4000;
    if (!give_up) return;
    og_log("live check failed (%s): capmode %x slot %d VD %d",
           og_state == OG_ACTIVE ? "active" : "after start", cap, slot, vd);
    og_reason = og_state == OG_ACTIVE ? "Unexpected movie mode" : "Not taking effect";
    NotifyBox(6000, "Open gate: %s.\nTurned off: switch to photo mode once.", og_reason);
    og_ui = 0;
    og_state = OG_REMOVING;
    og_bad_samples = 0;
}

static void og_step(void)
{
    static int logged_vmode_changes = -1;
    if (og_debug && og_vmode_changes != logged_vmode_changes)
    {
        logged_vmode_changes = og_vmode_changes;
        og_log("PROP_VIDEO_MODE #%d (%d calls, len %d): %x %x %x %x (path %x)", og_vmode_changes,
               og_vmode_calls, og_vmode_len, og_vmode[0], og_vmode[1], og_vmode[2], og_vmode[3], og_path());
    }
    og_handle_lost_locked();
    og_recall_setting();
    if (og_removed_notice)
    {
        og_removed_notice = 0;
        og_log("removed by a setting change: %s", og_reason);
        NotifyBox(4000, "Open gate turned off:\n%s", og_reason);
    }
    og_observe_setting();
    og_finish_deferred();

    switch (og_state)
    {
        case OG_OFF:
            if (og_ui && !og_pending_deferred) { og_state = OG_ARMED; og_reason = ""; }
            break;

        case OG_ARMED:
            if (!og_ui) { og_state = OG_OFF; break; }
            {
                int col = og_setting_col;   /* read once: the handler may zero it */
                const struct og_rate * r = og_rate_of_col(col);
                const char * wait =
                    og_pending_deferred ? "deferred restore" : og_hdmi ? "HDMI" :
                    !lv ? "no LV" : !og_safe_point() ? "not a safe point" :
                    !r ? "setting unknown" : og_setting_seq != og_vmode_changes ? "setting changed" :
                    !og_fps_ok(col) ? "fps" : 0;
                static const char * logged_wait;
                if (wait != logged_wait)
                {
                    logged_wait = wait;
                    if (wait) og_log("waiting: %s (path %x, col %d)", wait, og_path(), col);
                }
                if (wait) break;
                if (!og_alloc_copies()) { og_reason = "Not enough memory"; og_ui = 0; og_state = OG_OFF; break; }
                if (!og_prepare(r) || !og_commit())
                {
                    og_log("apply refused: %s", og_reason);
                    NotifyBox(5000, "Open gate refused:\n%s", og_reason);
                    og_ui = 0;
                    og_state = OG_OFF;
                    break;
                }
                og_rate = r;
                og_active_since = 0;
                og_bad_samples = 0;
                og_state = OG_APPLIED;
                og_log("applied: 4K %s", r->name);
                NotifyBox(4000, "Open gate ready (4K %s):\nswitch to movie mode.", r->name);
            }
            break;

        case OG_APPLIED:
        case OG_ACTIVE:
            if (!og_ui) { og_state = OG_REMOVING; break; }
            og_check_live();
            break;

        case OG_REMOVING:
            if (og_ui) { og_state = OG_APPLIED; og_active_since = 0; break; }
            if (og_safe_point())
            {
                og_remove();
                og_state = OG_OFF;
                og_reason = "";
                og_log("removed (menu OFF)");
            }
            break;
    }
}

static void og_task(void * unused)
{
    TASK_LOOP
    {
        msleep(100);
        if (take_semaphore(og_lock, 500) != 0) continue;
        og_step();
        give_semaphore(og_lock);
    }
}

/* ------------------------------------------------------------------------
 * menu
 * ------------------------------------------------------------------------ */

static MENU_UPDATE_FUNC(og_update)
{
    switch (og_state)
    {
        case OG_ACTIVE:
            MENU_SET_VALUE("ON");
            MENU_SET_RINFO("4096x2736 %s", og_rate ? og_rate->name : "");
            break;
        case OG_APPLIED:
            MENU_SET_VALUE("Ready");
            MENU_SET_WARNING(MENU_WARN_INFO, "Ready: switch to movie mode (4K %s).", og_rate ? og_rate->name : "");
            break;
        case OG_REMOVING:
            MENU_SET_VALUE("Turning off");
            MENU_SET_WARNING(MENU_WARN_INFO, "Switch to photo mode once to finish turning off.");
            break;
        case OG_ARMED:
            MENU_SET_VALUE("Waiting");
            if (og_hdmi)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Disconnect HDMI.");
            else if (og_setting_bad == 2)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Turn Canon movie cropping OFF.");
            else if (og_setting_bad == 3)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Use 25p, 29.97p or 23.98p (24.00p / 30.00p not supported).");
            else if (og_setting_bad || !og_setting_col)
                MENU_SET_WARNING(MENU_WARN_ADVICE, "Set Canon movie size to 4K 25p / 29.97p / 23.98p and enter movie mode once.");
            else
                MENU_SET_WARNING(MENU_WARN_ADVICE, "Switch to photo mode once to apply.");
            break;
        default:
            if (og_reason && og_reason[0]) MENU_SET_WARNING(MENU_WARN_INFO, "Last: %s", og_reason);
            break;
    }
    if (RECORDING && og_state != OG_OFF)
        MENU_SET_WARNING(MENU_WARN_INFO, "Recording: changes wait until it stops.");
}

static struct menu_entry og_menu[] =
{
    {
        .name    = "Open gate 4K",
        .priv    = &og_ui,
        .max     = 1,
        .update  = og_update,
        .help    = "Whole sensor (3:2) in 4K: 4096x2736 HEVC 10-bit, 25p / 29.97p / 23.98p.",
        .help2   = "Canon: 4K 25/29.97/23.98p, 10-bit, crop OFF. Enter movie, then photo mode once.",
        .submenu_width = 650,
        .children = (struct menu_entry[]) {
            {
                .name   = "Debug log",
                .priv   = &og_debug,
                .max    = 1,
                .help   = "Write ML/LOGS/OPENGATE.LOG (events only).",
            },
            MENU_EOL,
        },
    },
};

/* ------------------------------------------------------------------------
 * module
 * ------------------------------------------------------------------------ */

static unsigned int og_init(void)
{
    /* R6 only; the firmware is checked word by word: every ROM and RAM value is compared
     * with its 1.9.0 stock value before anything is written, so other versions refuse */
    if (!is_camera("R6", "*"))
    {
        printf("opengate: R6 only\n");
        return CBR_RET_ERROR;
    }
    og_lock = create_named_semaphore("opengate", 1);
    menu_add("Movie", og_menu, COUNT(og_menu));
    task_create("opengate_task", 0x1a, 0x3000, og_task, 0);
    return 0;
}

static unsigned int og_deinit(void)
{
    /* modules are not unloaded at runtime on this port; remove anyway if possible */
    if (take_semaphore(og_lock, 1000) == 0)
    {
        if (og_removal_needed() && !RECORDING) og_remove();
        give_semaphore(og_lock);
    }
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(og_init)
    MODULE_DEINIT(og_deinit)
MODULE_INFO_END()

MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_VIDEO_MODE)
    MODULE_PROPHANDLER(PROP_HDMI_CHANGE)
MODULE_PROPHANDLERS_END()
