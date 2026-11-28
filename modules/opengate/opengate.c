/** \file
 * Open gate 4K for the R6 1.9.0: the whole sensor height in 4K, recorded at
 * 4096x2736 (3:2) at 25p, 29.97p or 23.98p, or at 3840x2880 (4:3, the centre 4864x3648 of
 * the sensor) at 25p; HEVC 4:2:2 10-bit or H.264, level 6.1.
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
 * is not running and the MemoryNavigator 4K maps are not active. The movie setting comes
 * from Canon's PROP_MOVIE_CONTROL_PARAM (kept by the core from boot) and is confirmed by
 * Canon's own 4K <= 30p LiveView (capture mode 0x1B, sensor slot 10). A change of the
 * size, rate, crop or recording setup removes the patches from the property handler,
 * before Canon restarts the movie LiveView; the next safe point re-applies them.
 *
 * Canon's in-camera player refuses these clips, and its ORCA resize stops the camera on
 * them. The Playback entry (own patch set, any open gate mode, ON by default) lets the
 * player open them, makes its resize read every second line of the whole frame and keeps
 * the resize's split parts at an even width (research/opengate/player/PLAYER-2736-v2.md).
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

/* src/patch_mmu.h (not included by patch.h for modules) */
extern int mmu_reserve_pages_for_patches(const struct patch * patches, uint32_t count);
#define OG_E_NO_PAGES 0x10000           /* not an E_PATCH_* code: the reservation failed */
/* Canon's AllocateMemory pool (src/mem.c): the runtime MMU pages come from it */
extern int GetMemoryInformation(int * total, int * free);

static int og_mem_free(void)
{
    int total = 0, free = 0;
    GetMemoryInformation(&total, &free);
    return free;
}

#define RD32(a) (*(volatile uint32_t *)(a))
#define RD16(a) (*(volatile uint16_t *)(a))

/* ------------------------------------------------------------------------
 * state
 * ------------------------------------------------------------------------ */

enum og_state { OG_OFF, OG_ARMED, OG_APPLIED, OG_ACTIVE, OG_REMOVING };

/* menu: the user's choice (not saved: OFF at boot), OFF / 3:2 / 4:3 */
enum og_mode { OG_MODE_OFF, OG_MODE_32, OG_MODE_43 };
static int og_ui;
static int og_mode;                     /* the mode of the patches in place (set at apply) */
static const char * const og_mode_size[] = { "", "4096x2736", "3840x2880" };
static CONFIG_INT("opengate.debug", og_debug, 0);   /* menu: write ML/LOGS/OPENGATE.LOG */
static volatile int og_state = OG_OFF;
static const char * og_reason = "";     /* why the last apply was refused or removed */

/* the movie setting, learnt from Canon's stock 4K <= 30p LV */
static volatile int og_setting_col;      /* timing column 2/3/4, 0 = unknown or not OK */
static volatile int og_setting_bad;      /* 1 not 4K <= 30p, 2 crop, 3 fps not supported */
static volatile int og_setting_seq;      /* og_setting_changes when the setting was learnt */
static volatile int og_hdmi;
static const struct og_rate * og_rate;   /* rate applied, then the live one (all columns are patched) */

static struct semaphore * og_lock;      /* worker task vs. property handler removal */

/* ------------------------------------------------------------------------
 * debug log (off by default; one line per event, never from a property handler)
 * ------------------------------------------------------------------------ */

static volatile int og_in_handler;      /* set while a property handler runs: no file access */
static struct semaphore * og_log_lock;  /* the worker and the playback diagnostics task log */

static void og_log(const char * fmt, ...)
{
    /* no card access while Canon shuts down: the LV stop at power off logs a line, and a
     * write racing the card shutdown is the suspect of two power-off hangs (run 53) */
    if (!og_debug || og_in_handler || ml_shutdown_requested) return;
    char buf[240];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(buf, sizeof(buf), "%d ", get_ms_clock());
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    if (og_log_lock && take_semaphore(og_log_lock, 500) != 0) return;
    FILE * f = FIO_CreateFileOrAppend("ML/LOGS/OPENGATE.LOG");
    if (f)
    {
        FIO_WriteFile(f, buf, strlen(buf));
        FIO_WriteFile(f, "\n", 1);
        FIO_CloseFile(f);
    }
    if (og_log_lock) give_semaphore(og_log_lock);
}

/* ------------------------------------------------------------------------
 * DryOS assert hook (with the debug log only): file, line and caller of an assert
 * (Err 70) go to the log. The port has no ML assert logger (CONFIG_CRASH_LOG is off
 * on R6). The handler may run in any context: it only copies into a static buffer,
 * and the worker writes it. Same hook as the ogprobe module of the encoder tests.
 * ------------------------------------------------------------------------ */

#define OG_ASSERT_HANDLER_PTR 0x4000    /* DRYOS_ASSERT_HANDLER, platform/R6.190/consts.h */
static int (*og_old_assert)(char *, char *, int, int);
static volatile int og_assert_hit;      /* 1 = to write, 2 = written */
static char og_assert_buf[240];
static uint32_t og_assert_ctx[4];       /* filled by the worker: state at the last step */

static void og_copy_str(char * dst, int n, const char * src)
{
    /* Canon's assert strings are in ROM; ours in RAM */
    uint32_t a = (uint32_t) src;
    int i = 0;
    if ((a >= 0x1000 && a < 0x40000000) || (a >= 0x40001000 && a < 0x80000000) || a >= 0xE0000000)
        for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int og_assert_handler(char * msg, char * file, int line, int arg4)
{
    if (!og_assert_hit)
    {
        char m[80], f[80];
        og_copy_str(m, sizeof(m), msg);
        og_copy_str(f, sizeof(f), file);
        snprintf(og_assert_buf, sizeof(og_assert_buf),
                 "ASSERT %s at %s:%d (arg4 %x) lr %x; last step: state %d lv %d path %x capmode %x",
                 m, f, line, arg4, __builtin_return_address(0), og_assert_ctx[0], og_assert_ctx[1],
                 og_assert_ctx[2], og_assert_ctx[3]);
        og_assert_hit = 1;
    }
    return og_old_assert(msg, file, line, arg4);
}

static void og_assert_hook_install(void)
{
    if (og_old_assert) return;
    uint32_t old = RD32(OG_ASSERT_HANDLER_PTR);
    if (old < 0xE0000000) return;       /* not Canon's handler: leave it alone */
    og_old_assert = (void *) old;
    RD32(OG_ASSERT_HANDLER_PTR) = (uint32_t) og_assert_handler;
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

/* LiveView conductor (MC-ENTRY.md 6): *(0x11404) + 0x38 is 1 from CondStop (before
 * SDRV_ShutdownDevice) to LibCond Start (just before SDRV_StartupDevice): the sensor is off.
 * ML's lv flag does not follow this stop (menus, setting changes) */
#define OG_LVCOND_PTR   0x11404
#define OG_LVCOND_SUSP  0x38

static int og_lv_suspended(void)
{
    uint32_t o = og_rd(OG_LVCOND_PTR);
    return og_ram_ok(o + OG_LVCOND_SUSP, 4) && RD32(o + OG_LVCOND_SUSP) == 1;
}

/* Canon's stored 24.00p setting (fps24 module, FPS24.md P4n): model+0x610, -1 unreadable */
#define OG_CC_PTR     0x10C28
#define OG_MODEL_OFF  0x34FC
#define OG_24P_OFF    0x610
static int og_24p_stored(void)
{
    uint32_t model = og_rd(og_rd(og_rd(OG_CC_PTR) + 8) + OG_MODEL_OFF);
    return og_ram_ok(model, OG_24P_OFF + 4) ? (int) og_rd(model + OG_24P_OFF) : -1;
}

/* debug log only: Canon's DryosDebugMsg RAM ring (S = *0x52A8, "KUWA" handles at
 * S+0x10/0x14/0x18; PLAYER-2736.md 9), written to ML/LOGS/OGASSERT.BIN by the worker
 * right after an assert, in the OGDM format of player/dmring.py (as fps24's dump) */
#define OG_DM_PTR    0x52A8
#define OG_DM_S_LEN  0x348
#define OG_DM_MAGIC  0x4157554B
#define OG_DM_SLOT   0x54

static uint32_t og_dm_ring(uint32_t s, int k, uint32_t * len)
{
    uint32_t h = og_rd(s + 0x10 + 4 * k);
    if (!og_ram_ok(h, 0x80) || og_rd(h) != OG_DM_MAGIC) return 0;
    uint32_t n = 0;
    for (int sub = 0; sub < 2; sub++)
    {
        uint32_t off = og_rd(h + 4 * (2 + 15 * sub)), cnt = og_rd(h + 4 * (4 + 15 * sub));
        if (off < 0x80 || off > 0x100000 || cnt > 0x4000) return 0;
        n = MAX(n, off + cnt * OG_DM_SLOT);
    }
    if (!og_ram_ok(h, n)) return 0;
    *len = n;
    return h;
}

static int og_dm_dump(const char * name)
{
    uint32_t s = og_rd(OG_DM_PTR);
    if (!og_ram_ok(s, OG_DM_S_LEN) || !og_rd(s + 0x0C)) return 0;
    uint32_t h[3], n[3], nb = 1;
    for (int k = 0; k < 3; k++) { h[k] = og_dm_ring(s, k, &n[k]); if (h[k]) nb++; }
    FILE * f = FIO_CreateFile(name);
    if (!f) return 0;
    uint32_t hdr[4] = { 0x4D44474F, 1, s, nb };    /* "OGDM", version, S, blocks */
    FIO_WriteFile(f, hdr, sizeof(hdr));
    uint32_t b[2] = { s, OG_DM_S_LEN };
    FIO_WriteFile(f, b, sizeof(b));
    FIO_WriteFile(f, (void *) s, OG_DM_S_LEN);     /* FIO_WriteFile uses the uncached alias */
    for (int k = 0; k < 3; k++)
    {
        if (!h[k]) continue;
        b[0] = h[k]; b[1] = n[k];
        FIO_WriteFile(f, b, sizeof(b));
        FIO_WriteFile(f, (void *) h[k], n[k]);
    }
    FIO_CloseFile(f);
    return nb;
}

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

#define OG_MAX_RAM 128          /* 71 in use (3:2), 77 (4:3) */
#define OG_MAX_ROM 96           /* 63 in use (both modes); og_apply_rom keeps 3 arrays of this on the 12 KB task stack */
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

/* the mode being prepared: 4:3 takes the og_words43 value of a word, and a 4:3 value equal to
 * the stock value is not patched */
static int og_prep_mode;

static void og_add_words(const struct og_word * w, int n)
{
    for (int i = 0; i < n; i++)
    {
        uint32_t v = w[i].val;
        if (og_prep_mode == OG_MODE_43)
        {
            int alt = 0;
            for (int k = 0; k < COUNT(og_words43); k++)
                if (og_words43[k].addr == w[i].addr) { v = og_words43[k].val; alt = 1; }
            if (alt && v == w[i].stock) continue;
        }
        og_add_rom(w[i].addr, w[i].stock, v, w[i].insn, w[i].what);
    }
}

/* ROM words in one 64 KB page less than this apart become one multi-byte patch:
 * MAX_PATCHES is 48 on R6 and the 60-min clip option alone takes 15 (RESULTS run 44) */
#define OG_MERGE_GAP 0x100

/* one all-or-nothing patch set from a word list (the open gate's, or the player's below);
 * patch_addr receives the addresses to unpatch */
static int og_apply_words(const struct og_word * words, int n, uint32_t * patch_addr, int * n_patch)
{
    int idx[OG_MAX_ROM];
    if (n > OG_MAX_ROM) return E_PATCH_TOO_MANY_PATCHES;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (words[idx[j]].addr < words[idx[i]].addr) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }

    struct patch p[OG_MAX_ROM];
    uint8_t * bufs[OG_MAX_ROM];
    int np = 0, err = 0;
    for (int i = 0; i < n && !err; )
    {
        uint32_t start = words[idx[i]].addr, end = start + 4;
        int k = i + 1;
        while (k < n && (words[idx[k]].addr & 0xFFFF0000) == (start & 0xFFFF0000)
               && words[idx[k]].addr >= end && words[idx[k]].addr - end < OG_MERGE_GAP)
        {
            end = words[idx[k]].addr + 4;
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
            const struct og_word * w = &words[idx[m]];
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
                                     .description = words[idx[i]].what };
        else
            p[np] = (struct patch) { .addr = (uint8_t *) start, .old_values = oldb, .new_values = newb,
                                     .size = len, .is_instruction = insn, .description = words[idx[i]].what };
        patch_addr[np] = start;
        np++;
        i = k;
    }
    /* runtime MMU pages for what these patches still need (pages remapped before, by us or
     * another feature, are reused: a re-enable adds nothing) */
    /* the core allocates in small blocks (no ~700 KB AllocateMemory block: one failed once,
     * run 42) and keeps what it added if one fails. Reserving and applying are not atomic:
     * if another task took the free pages in between, reserve again once (as heat_timer.c) */
    for (int attempt = 0; attempt < 2 && !err; attempt++)
    {
        int mem0 = og_mem_free();
        int e = mmu_reserve_pages_for_patches(p, np);
        int mem1 = og_mem_free();
        /* free memory unchanged: no page added (all reused) */
        og_log("mmu_reserve_pages_for_patches(%d) = %d, AllocateMemory free %d -> %d (%d)",
               np, e, mem0, mem1, mem1 - mem0);
        if (e) { err = OG_E_NO_PAGES; break; }
        err = apply_patches(p, np);         /* copies the arrays; all or nothing */
        if (!(err & E_PATCH_BAD_MMU_PAGE)) break;
        if (attempt == 0) err = 0;          /* retry */
    }
    for (int b = 0; b < np; b++) free(bufs[b]);
    og_log("rom: %d words in %d patches: %x", n, np, err);
    *n_patch = err ? 0 : np;
    return err;
}

static void og_unpatch_all(uint32_t * patch_addr, int * n_patch)
{
    for (int i = *n_patch - 1; i >= 0; i--)
    {
        int e = unpatch_memory(patch_addr[i]);
        if (e) og_log("unpatch %08x: %x", patch_addr[i], e);
    }
    *n_patch = 0;
}

static int og_apply_rom(void)
{
    return og_apply_words(og_rom, og_n_rom, og_rom_patch, &og_n_rom_patch);
}

static void og_remove_rom(void)
{
    og_unpatch_all(og_rom_patch, &og_n_rom_patch);
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
    uint16_t vin, vout;         /* develop input rows (sensor area) and stored rows */
    uint16_t b0, b1;            /* develop input bands: b0 = 0 mod 8, b0 + b1 = vin */
    uint16_t stock_vin, stock_vout, stock_b0, stock_b1;
    uint16_t hout;              /* develop output width; the store keeps the centre 4096 */
    uint16_t vmargin;           /* develop rows above / below the stored frame */
};
/* 3:2: the whole 5472x3648 to 4096x2736 */
static const struct og_geometry og_geo32 = { 3648, 2736, 2200, 1448, 2886, 2160, 1504, 1382, 4096, 0 };
/* 4:3: 5472x3648 to 4320x2882 (1.2667 both ways), the store 4096x2880 at x 112 (112 + 2048 =
 * 4320 / 2, the tile split on the develop pipe boundary) from row 1: with the offset table, a
 * store from row 0 took a constant light-blue row from above the frame (6L0A8859) */
static const struct og_geometry og_geo43 = { 3648, 2880, 2200, 1448, 2886, 2160, 1504, 1382, 4320, 1 };
static uint32_t og_post_ofs[3];     /* 4:3: MvPostDevelop offset table, format of E1CA7A4C */

/* ------------------------------------------------------------------------
 * prepare: build every copy and the change list, check every stock value.
 * Writes nothing to Canon memory. Returns 0 or sets og_reason.
 * ------------------------------------------------------------------------ */

#define FAIL(why) do { og_reason = (why); og_log("prepare: %s", why); return 0; } while (0)

static int og_win_frame;        /* the prepared mode shows a 4:3 frame (core window records) */

static int og_prepare(const struct og_rate * r, int mode)
{
    og_n_ram = og_n_rom = 0;
    og_list_error = 0;
    og_win_frame = 0;
    og_prep_mode = mode;
    int m43 = mode == OG_MODE_43;
    const struct og_geometry * g = m43 ? &og_geo43 : &og_geo32;
    /* develop rows: the stored rows plus the margin above and below */
    uint32_t vdev = g->vout + 2u * g->vmargin;

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
        if (m43)
        {
            /* LV YUV slots: deferred like the raw entry */
            uint32_t y = mnav + subs[k] + 0x10 * OG_MNAV_YUV;
            if (RD32(y) != 6 || RD32(y + 4) != 0) FAIL("MemoryNavigator YUV entry unexpected");
            og_add_ram(y + 8, 4, OG_YUV_SLOT_STOCK, OG_YUV_SLOT_43, 1);
            og_add_ram(y + 12, 4, OG_YUV_N_STOCK, OG_YUV_N_43, 1);
        }
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

    /* timing cells of every supported column (HD and SetP/cartridge as slot 9), not only
     * the current one: a PAL/NTSC or fps change inside 4K <= 30p restarts the movie LV on
     * another column, and a stock column under the full-height readout froze the camera
     * (run 52). The encoder rows already cover the three rates. */
    for (int i = 0; i < COUNT(og_rates); i++)
    {
        const struct og_rate * t = &og_rates[i];
        int col = t->col;
        if (RD16(OG_TCELL(0xF00, col)) != OG_T_FLAG) FAIL("timing table unexpected");
        og_add_ram(OG_TCELL(0x000, col), 2, t->fl_stock, t->fl, 0);
        og_add_ram(OG_TCELL(0x280, col), 2, OG_T_SETP_STOCK, OG_T_SETP, 0);
        og_add_ram(OG_TCELL(0x500, col), 2, OG_T_ROD_STOCK, OG_T_ROD, 0);
        og_add_ram(OG_TCELL(0x780, col), 2, t->rps_stock, t->rps, 0);
        og_add_ram(OG_TCELL(0xA00, col), 2, OG_T_HD_STOCK, OG_T_HD, 0);
        og_add_ram(OG_TCELL(0xC80, col), 2, OG_T_HDUS_STOCK, OG_T_HDUS, 0);
        og_add_ram(OG_TCART(col), 4, OG_T_CART_STOCK, OG_T_CART, 0);
    }

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
    struct og_scale sc = og_scale_rule(g->vin, vdev, g->b0);
    *(uint32_t *) (dev + 0x28) = g->b0;
    *(uint32_t *) (dev + 0x2C) = g->b1;
    *(uint32_t *) (dev + 0x8E0) = vdev;
    *(uint32_t *) (dev + 0x8E4) = g->vin;
    for (int pipe = 0; pipe < 2; pipe++)
    {
        int n = og_vfields(g->b0, g->b1, vf_off, vf_val);
        for (int i = 0; i < n; i++) *(uint32_t *) (dev + OG_DEV_PIPE1 * pipe + vf_off[i]) = vf_val[i];
        n = og_vdma(g->b0, g->b1, vf_off, vf_val);
        for (int i = 0; i < n; i++) *(uint32_t *) (dev + OG_DEV_DMA1 * pipe + vf_off[i]) = vf_val[i];
        og_scale_write(dev + OG_DEV_SCALE1 * pipe, &sc);
    }
    if (g->hout != OG_HOUT_STOCK)
    {
        /* output width: the H rule must reproduce the stock 4096 record first */
        struct og_hscale hst = og_hscale_rule(OG_HIN, OG_HOUT_STOCK, OG_HB0);
        struct og_hscale hn = og_hscale_rule(OG_HIN, g->hout, OG_HB0);
        if (*(uint32_t *) (dev + 0x18) != OG_HOUT_STOCK || *(uint32_t *) (dev + 0x8D8) != OG_HOUT_STOCK)
            FAIL("develop record unexpected (H)");
        for (int pipe = 0; pipe < 2; pipe++)
            if (!og_hscale_matches(dev + OG_DEV_SCALE1 * pipe, &hst)) FAIL("develop rule mismatch (H scale)");
        *(uint32_t *) (dev + 0x18) = g->hout;
        *(uint32_t *) (dev + 0x8D8) = g->hout;
        for (int pipe = 0; pipe < 2; pipe++) og_hscale_write(dev + OG_DEV_SCALE1 * pipe, &hn);
        og_log("develop %dx%d (H bands %d/%d, V bands %d/%d), store 4096x%d at x %d y %d",
               g->hout, vdev, hn.n1, hn.n2, sc.n1, sc.n2, g->vout, (g->hout - OG_HOUT_STOCK) / 2, g->vmargin);
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
    uint32_t gcd = og_gcd(vdev, g->vin);
    *(uint32_t *) (post + 0x38) = g->vout / 2;      /* stored tiles: Vout rows */
    *(uint32_t *) (post + 0x40) = g->vout / 2;
    *(uint32_t *) (post + 0x5C) = vdev;             /* frame: the develop output */
    *(uint32_t *) (post + 0x68) = vdev - 1u;
    *(uint32_t *) (post + 0xC0) = vdev / gcd;
    *(uint32_t *) (post + 0xC8) = g->vin / gcd;
    *(uint32_t *) (post + 0xAC) = (uint32_t) crop;
    *(uint32_t *) (crop + 0x04) = (vdev - g->stock_vout) / 2;
    *(uint32_t *) (crop + 0x2C) = (vdev - g->stock_vout) / 2;
    if (g->hout != OG_HOUT_STOCK)
    {
        /* input / frame width = the develop output, H ratio hout:5472, the centre 4096 stored
         * through the offset table, as Canon's path 4 (E1CA7A4C: {0x00010003, y << 16 | x, 0}) */
        if (*(uint32_t *) (post + 0x18) != OG_HOUT_STOCK || *(uint32_t *) (post + 0x1C) != OG_HOUT_STOCK
            || *(uint32_t *) (post + 0x58) != OG_HOUT_STOCK || *(uint32_t *) (post + 0x64) != OG_HOUT_STOCK - 1u
            || *(uint32_t *) (post + 0xBC) != 128 || *(uint32_t *) (post + 0xC4) != 171
            || *(uint32_t *) (post + 0xB0) != OG_POST_OFS_TBL || *(uint32_t *) (post + 0xB4) != OG_POST_OFS_TBL)
            FAIL("post-develop record unexpected (H)");
        *(uint32_t *) (post + 0x18) = g->hout;
        *(uint32_t *) (post + 0x1C) = g->hout;
        *(uint32_t *) (post + 0x58) = g->hout;
        *(uint32_t *) (post + 0x64) = g->hout - 1u;
        uint32_t hg = og_gcd(g->hout, OG_HIN);
        *(uint32_t *) (post + 0xBC) = g->hout / hg;
        *(uint32_t *) (post + 0xC4) = OG_HIN / hg;
        og_post_ofs[0] = OG_POST_OFS_FLAGS;
        og_post_ofs[1] = ((uint32_t) g->vmargin << 16) | ((g->hout - OG_HOUT_STOCK) / 2);
        og_post_ofs[2] = 0;
        *(uint32_t *) (post + 0xB0) = (uint32_t) og_post_ofs;
        *(uint32_t *) (post + 0xB4) = (uint32_t) og_post_ofs;
    }
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

    if (m43)
    {
        og_add_words(og_enc43_words, COUNT(og_enc43_words));
        /* LCD / EVF 4:3 window: the core owns the window records (Display > Anamorphic,
         * CONFIG_LV_DISPLAY_WINDOW) and sizes the image box of records 2 / 0xC for a 4:3
         * frame at the commit; ORCA reads them at the movie LV setup. Stock here: the last
         * removal gave them back */
        for (int i = 0; i < 12; i++)
            if (RD32(OG_WIN_LCD43 + 4 * i) != og_win_lcd43[i] || RD32(OG_WIN_EVF43 + 4 * i) != og_win_evf43[i])
                FAIL("display window records unexpected");
        og_win_frame = 1;
    }

    if (og_list_error) FAIL("internal: change list full");
    if (!og_check_ram()) FAIL("a Canon value is not stock");
    og_log("prepare %s %s: %d RAM words, %d ROM words", og_mode_size[mode], r->name, og_n_ram, og_n_rom);
    return 1;
}

#undef FAIL

/* ------------------------------------------------------------------------
 * commit / remove (worker task, or the property handler under og_lock)
 * ------------------------------------------------------------------------ */

/* the fps24 module (Canon's 24.00p row) is in when its row caption word is patched. The two
 * features exclude each other (24.00p uses its own sensor timing column and fps index): the
 * open gate waits while fps24 is ON; turning that entry off is enough */
static int og_fps24_in(void)
{
    return RD32(OG_FPS24_ROW_WORD) != 0;
}

static int og_commit(void)
{
    if (RECORDING) { og_reason = "Stop recording first"; return 0; }
    int e = og_apply_rom();
    if (e) { og_reason = e == E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active"
                       : e == E_PATCH_CANNOT_MALLOC || e == OG_E_NO_PAGES ? "Not enough memory for the patches"
                       : "ROM patch refused"; return 0; }
    if (!og_check_ram())     /* re-check right before writing */
    {
        og_remove_rom();
        og_reason = "A Canon value changed";
        return 0;
    }
    for (int i = 0; i < og_n_ram; i++) og_set(og_ram[i].addr, og_ram[i].size, og_ram[i].val);
    /* the 4:3 frame for the LCD / EVF window, before the movie LV setup that reads it
     * (also desqueezed when Display > Anamorphic is on) */
    if (og_win_frame)
        og_log("display window 4:3: %s", lv_display_window_set_frame(4, 3) ? "set" : "refused by the core");
    og_log("commit: done");
    return 1;
}

/* outside a safe point the deferred words (MemoryNavigator raw entries, stage-8 table
 * slots) stay until the next one: they are harmless alone */
static int og_pending_deferred;

static int og_last_left;        /* words left by the last removal (logged by the worker) */

static void og_remove(void)
{
    int safe = og_safe_point();
    og_remove_rom();
    /* the window records back to stock (or to the plain anamorphic layout): the next LV
     * setup is Canon's own mode; never read with the open-gate words, as they go now */
    lv_display_window_set_frame(0, 0);
    int left = og_restore_ram(safe);
    og_pending_deferred = left > 0;
    og_last_left = left;
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
 * Canon's player and the open gate clips (og_r6_190.h, og_pb_words; PLAYER-2736-v2.md).
 * Its own patch set and menu entry, independent of the open gate mode, applied while the
 * module is loaded and the entry is on, so old clips play with the open gate OFF too.
 * Applied and removed only outside playback (Canon's player is not set up then).
 *
 * The functions below run inside Canon's player code (its tasks), reached by a bl from the
 * patched site to an 8-byte jump in the cave, and return with bx lr to the instruction
 * after the site. Each one does what the replaced instructions did, with Canon's values for
 * clips of 2160 rows or less, and uses only the registers noted (checked dead at the site,
 * pbwords.py). og_pb_diag counts what they did, for the debug log.
 * ------------------------------------------------------------------------ */

struct og_pb_diag
{
    uint32_t d_count, d_w, d_h;     /* +0  resize set-ups of a tall clip, last w / h */
    uint32_t fit_count;             /* +12 display fits of a tall clip */
    uint32_t k_pass, k_refuse;      /* +16 ChkFrameRate: tall sizes let through / refused */
    uint32_t k_w, k_h;              /* +24 last size seen there (not 2160 rows) */
    uint32_t rot_tall;              /* +32 convert-buffer steps of a tall clip (2 buffers) */
    uint32_t n_count, n_h;          /* +36 decoded-frame set-ups of a tall clip (4 frames), last h */
};
struct og_pb_diag og_pb_diag __attribute__((used));    /* written by the asm below */

#define OG_STR2(x) #x
#define OG_STR(x) OG_STR2(x)

extern void og_pb_K(void), og_pb_N(void), og_pb_W(void), og_pb_R(void), og_pb_D(void), og_pb_T(void),
            og_pb_F(void);

/* Thumb-2 (Canon's code is Thumb-2; the module is built for ARMv5TE, so the block sets the
 * CPU for itself and goes back to ARM mode at the end) */
asm(
    ".text\n"
    ".syntax unified\n"
    ".cpu cortex-a9\n"
    ".thumb\n"
    ".align 2\n"

    /* K, at ChkFrameRate's 4096 and 3840 size checks, for "cmp r1, r4; bne refuse":
     * r0 = width, r1 = height, r4 = 2160. 2160 rows go on as before; 4096x2736 and
     * 3840x2880 go on to Canon's frame-rate checks; anything else returns to Canon's
     * refusal (log "W=%ld,H=%ld", return 1). r0, r1 are kept (the refusal logs them) */
    ".global og_pb_K\n"
    ".type og_pb_K, %function\n"
    ".thumb_func\n"
    "og_pb_K:\n"
    "    cmp     r1, r4\n"
    "    beq     9f\n"
    "    push    {r2, r3}\n"
    "    ldr     r2, =og_pb_diag\n"
    "    str     r0, [r2, #24]\n"
    "    str     r1, [r2, #28]\n"
    "    cmp.w   r0, #0x1000\n"
    "    bne     1f\n"
    "    cmp.w   r1, #2736\n"
    "    b       2f\n"
    "1:  cmp.w   r1, #2880\n"               /* width 3840: only the two sites call K */
    "2:  beq     3f\n"
    "    ldr     r3, [r2, #20]\n"
    "    adds    r3, #1\n"
    "    str     r3, [r2, #20]\n"
    "    pop     {r2, r3}\n"
    "    ldr.w   lr, =" OG_STR(OG_PB_REFUSE) "\n"
    "    bx      lr\n"
    "3:  ldr     r3, [r2, #16]\n"
    "    adds    r3, #1\n"
    "    str     r3, [r2, #16]\n"
    "    pop     {r2, r3}\n"
    "    cmp     r1, r1\n"                  /* Z = 1, as after Canon's equal compare */
    "9:  bx      lr\n"

    /* N, in the decoded-frame set-up, for "movs r0, #6; str r0, [r5, #0x18]": r9 = the
     * movie info {w, h, ...}, r5 = the frame set. 4 frames above 2160 rows. r0 (r1, r2
     * saved); the kept "str r0, [sp]" after the site passes the same count to the allocator */
    ".global og_pb_N\n"
    ".type og_pb_N, %function\n"
    ".thumb_func\n"
    "og_pb_N:\n"
    "    ldr.w   r0, [r9, #4]\n"
    "    cmp.w   r0, #2160\n"
    "    bls     1f\n"
    "    push    {r1, r2}\n"
    "    ldr     r2, =og_pb_diag\n"
    "    str     r0, [r2, #40]\n"
    "    ldr     r1, [r2, #36]\n"
    "    adds    r1, #1\n"
    "    str     r1, [r2, #36]\n"
    "    pop     {r1, r2}\n"
    "    movs    r0, #4\n"
    "    b       2f\n"
    "1:  movs    r0, #6\n"
    "2:  str     r0, [r5, #0x18]\n"
    "    bx      lr\n"

    /* T, in MVP_CheckTransCodeFile (Compress & save), for "ldr.w r3, [r0, #0x9c0]": r0 = the
     * player, r3 = the width for Canon's compares that follow (8192, 4096 = refused). Above
     * 2160 rows r3 = 4096, so Canon refuses these clips as it refuses DCI. r3 only */
    ".global og_pb_T\n"
    ".type og_pb_T, %function\n"
    ".thumb_func\n"
    "og_pb_T:\n"
    "    ldr.w   r3, [r0, #0x9c4]\n"
    "    cmp.w   r3, #2160\n"
    "    bhi     1f\n"
    "    ldr.w   r3, [r0, #0x9c0]\n"
    "    bx      lr\n"
    "1:  mov.w   r3, #0x1000\n"
    "    bx      lr\n"

    /* F, at the start of Canon's display fit E04035B8, reached by b.w (lr = its caller):
     * r0 = the movie info {w, h}, r1 = the display boxes (4 x 0x40; +0x28 the fitted out w
     * for the display), r3 = the resize channel records (4 x 0x1c: enabled, VRAM w, out w,
     * ...), [sp] = the 5th argument. For a clip taller than 2160 rows Canon's fit runs as a
     * call (its push.w, then its code; the 5th argument pushed again for it), then each
     * enabled output width is rounded down to a multiple of 4 in both the resize record and
     * the display box, so they agree: the resize's 2x2 split halves the width, and a part of
     * odd width kills the camera (3:2 in the 900x600 LCD box: 898, parts 449). Other clips:
     * Canon's push.w and on. r0, r1 as Canon returns them; r2, r3, ip */
    ".global og_pb_F\n"
    ".type og_pb_F, %function\n"
    ".thumb_func\n"
    "og_pb_F:\n"
    "    ldr.w   ip, [r0, #4]\n"
    "    cmp.w   ip, #2160\n"
    "    bls     og_pb_fit\n"
    "    push    {r1, r3, lr}\n"
    "    ldr     ip, [sp, #12]\n"           /* the 5th argument */
    "    push    {ip}\n"                    /* again at [sp]: 16 bytes, still 8-aligned */
    "    bl      og_pb_fit\n"
    "    add     sp, #4\n"
    "    pop     {r1, r3, lr}\n"
    "    push    {r0, r1, r4, r5}\n"
    "    ldr     r2, =og_pb_diag\n"
    "    ldr     r4, [r2, #12]\n"
    "    adds    r4, #1\n"
    "    str     r4, [r2, #12]\n"
    "    movs    r4, #4\n"
    "1:  ldr     r5, [r3]\n"
    "    cbz     r5, 2f\n"
    "    ldr     r5, [r3, #8]\n"
    "    bic     r5, r5, #3\n"
    "    str     r5, [r3, #8]\n"
    "    str     r5, [r1, #0x28]\n"
    "2:  adds    r3, #0x1c\n"
    "    adds    r1, #0x40\n"
    "    subs    r4, #1\n"
    "    bne     1b\n"
    "    pop     {r0, r1, r4, r5}\n"
    "    bx      lr\n"
    "og_pb_fit:\n"                          /* Canon's fit: its push.w, then its code */
    "    push.w  {r0, r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, ip, lr}\n"
    "    ldr.w   pc, =" OG_STR(OG_PB_FIT_RET) "\n"

    /* W (producer, for "ldr.w r0, [r4, #0x908]; adds r0, #1") and R (consumer, for
     * "movs r1, #3; ldr.w r0, [r4, #0x914]"): r4 = the player, r0 = the next index, r1 = the
     * number of convert buffers for the modulo that follows: 2 above 2160 rows, else 3.
     * r0, r1 (r2, r3 saved) */
    ".global og_pb_W\n"
    ".type og_pb_W, %function\n"
    ".thumb_func\n"
    "og_pb_W:\n"
    "    ldr.w   r0, [r4, #0x908]\n"
    "    adds    r0, #1\n"
    "    b       og_pb_mod\n"
    ".global og_pb_R\n"
    ".type og_pb_R, %function\n"
    ".thumb_func\n"
    "og_pb_R:\n"
    "    ldr.w   r0, [r4, #0x914]\n"
    "og_pb_mod:\n"
    "    ldr.w   r1, [r4, #0x9c4]\n"        /* the movie height (info at +0x9C0) */
    "    cmp.w   r1, #2160\n"
    "    bhi     1f\n"
    "    movs    r1, #3\n"
    "    bx      lr\n"
    "1:  push    {r2, r3}\n"
    "    ldr     r2, =og_pb_diag\n"
    "    ldr     r3, [r2, #32]\n"
    "    adds    r3, #1\n"
    "    str     r3, [r2, #32]\n"
    "    pop     {r2, r3}\n"
    "    movs    r1, #2\n"
    "    bx      lr\n"

    /* D, in the resize set-up (rsz mode 0), for "ldr r0, [r4]; str r0, [sp];
     * ldr r0, [r4, #4]; str r0, [sp, #4]": r4 = the input {w, h}, [sp] = the first two
     * words sent to ORCA (input line pitch and rows; Canon then copies them to the work
     * size, the region ORCA scales). Above 2160 rows: pitch 2w, rows h/2, region w x h/2:
     * ORCA reads every second line of the whole frame (2x2 split into w/2 x h/4 parts,
     * smaller than a DCI clip's); Canon's display fit, from the movie info, keeps the aspect.
     * The input height is set to the new one, so Canon's work-size copy follows it. The
     * output widths are rounded down to a multiple of 4 as og_pb_F does (a no-op after it;
     * kept so that no resize of a tall clip ever gets an odd split part).
     * r0-r3, ip (dead here, Canon sets them before use) */
    ".global og_pb_D\n"
    ".type og_pb_D, %function\n"
    ".thumb_func\n"
    "og_pb_D:\n"
    "    ldr     r0, [r4]\n"
    "    ldr     r1, [r4, #4]\n"
    "    cmp.w   r1, #2160\n"
    "    bls     9f\n"
    "    ldr     r2, =og_pb_diag\n"
    "    ldr     r3, [r2]\n"
    "    adds    r3, #1\n"
    "    str     r3, [r2]\n"
    "    str     r0, [r2, #4]\n"
    "    str     r1, [r2, #8]\n"
    "    lsrs    r1, r1, #1\n"
    "    str     r1, [r4, #4]\n"
    "    lsls    r0, r0, #1\n"
    /* the channels: 7 words from r4 + 0x10 {enabled, VRAM w, out w, ...} */
    "    add.w   r2, r4, #0x10\n"
    "    movs    r3, #4\n"
    "7:  ldr.w   ip, [r2]\n"
    "    cmp.w   ip, #0\n"
    "    beq     8f\n"
    "    ldr.w   ip, [r2, #8]\n"
    "    bic     ip, ip, #3\n"
    "    str.w   ip, [r2, #8]\n"
    "8:  adds    r2, #0x1c\n"
    "    subs    r3, #1\n"
    "    bne     7b\n"
    "9:  str     r0, [sp]\n"
    "    str     r1, [sp, #4]\n"
    "    bx      lr\n"
    ".ltorg\n"
    ".arm\n"            /* the CPU stays cortex-a9: GAS checks the Thumb-2 constants above at
                         * the end of the file (ARMv5TE code assembles the same) */
);

static void (* const og_pb_fn[])(void) = { og_pb_K, og_pb_N, og_pb_W, og_pb_R, og_pb_D, og_pb_T, og_pb_F };

static CONFIG_INT("opengate.play", og_pb_ui, 1);   /* menu, saved; ON by default */
static int og_pb_in;                    /* the patches are in */
static const char * og_pb_why = "";     /* why the last apply was refused */
#define OG_PB_N COUNT(og_pb_words)
static uint32_t og_pb_patch[OG_PB_N];
static int og_pb_n_patch;

static int og_pb_can_change(void)
{
    return !is_play_or_qr_mode() && !RECORDING;
}

static int og_pb_apply(void)
{
    struct og_word w[OG_PB_N];
    memcpy(w, og_pb_words, sizeof(w));
    for (int i = 0; i < OG_PB_N; i++)
        if (w[i].val == OG_PB_RUNTIME && w[i].addr >= OG_PB_JUMPS + 4)
            w[i].val = (uint32_t) og_pb_fn[(w[i].addr - OG_PB_JUMPS - 4) / 8] | 1;
    int e = og_apply_words(w, OG_PB_N, og_pb_patch, &og_pb_n_patch);
    og_log("play: apply %d words: %x (K %x N %x W %x R %x D %x T %x F %x)", OG_PB_N, e,
           og_pb_fn[0], og_pb_fn[1], og_pb_fn[2], og_pb_fn[3], og_pb_fn[4], og_pb_fn[5], og_pb_fn[6]);
    if (e) og_pb_why = e == E_PATCH_TOO_MANY_PATCHES ? "Too many ML patches active"
                     : e == E_PATCH_CANNOT_MALLOC || e == OG_E_NO_PAGES ? "Not enough memory for the patches"
                     : e == E_PATCH_OLD_VALUE_MISMATCH ? "ROM not stock"
                     : "ROM patch refused";
    return !e;
}

static void og_pb_remove(void)
{
    og_unpatch_all(og_pb_patch, &og_pb_n_patch);
    og_pb_in = 0;
    og_log("play: removed");
}

/* worker, og_lock held */
static void og_pb_step(void)
{
    if (og_pb_ui > 1) og_pb_ui = 1;     /* a test mode saved by a test build */
    if (!og_pb_ui == !og_pb_in || !og_pb_can_change()) return;
    if (og_pb_in) { og_pb_remove(); return; }
    if (og_pb_apply())
    {
        og_pb_in = 1;
        og_pb_why = "";
    }
    else
    {
        og_log("play: refused: %s", og_pb_why);
        NotifyBox(5000, "Open gate playback turned off:\n%s", og_pb_why);
        og_pb_ui = 0;
    }
}

/* ------------------------------------------------------------------------
 * playback diagnostics (debug log + playback on), own task. While Canon's player runs:
 * - every DebugMsg class at store level 1 for 10 s from the PLAY entry and from each new
 *   tall clip;
 * - the player's and ORCA's state words (og_dm_watch_list) whenever they change;
 * - the new lines of Canon's DebugMsg rings appended every 20 ms to one file per boot,
 *   ML/LOGS/OGRnnn.BIN (nnn counts boots, ML/LOGS/OGSESS.BIN), each PLAY entry and exit
 *   marked; decoded by player/dmlog.py. The lines are copied with the CPU (cached, so
 *   coherent with the other core) and the file is closed after each write, so the lines up
 *   to ~20 ms before a crash stay on the card. The first copy takes the whole ring;
 * - the og_pb_diag counters logged to OPENGATE.LOG as soon as they change;
 * - OGDM.ARM exists while playback runs: a session that did not end is logged at boot.
 * Nothing is patched for this.
 * ------------------------------------------------------------------------ */

#define RD8(a) (*(volatile uint8_t *)(a))
#define OG_DM_ALL_MS   10000
#define OG_DM_TIME     300000           /* capture for 5 min of playback per PLAY entry */
#define OG_DM_MAX_FILE (96 << 20)       /* and at most 96 MB per boot */
#define OG_DM_ARM_FILE "ML/LOGS/OGDM.ARM"
#define OG_DM_SESS     "ML/LOGS/OGSESS.BIN"
#define OG_DM_REC      (8 + OG_DM_SLOT) /* {u32 ring handle, u16 sub, u16 kind} + slot */
#define OG_DM_BUF_RECS 700
static uint8_t og_dm_all_saved[256];
static int og_dm_all_on;
static int og_dm_play_since;
static char og_dm_name[24];
static uint32_t og_dm_written;
static uint32_t og_dm_last[3][2];       /* per ring and sub-ring: writes already copied */
static int og_dm_first;                 /* the next copy takes the whole ring */
static uint8_t * og_dm_buf;

static void og_dm_all(int on)
{
    uint32_t s = og_rd(OG_DM_PTR);
    if (!og_ram_ok(s, OG_DM_S_LEN) || !og_rd(s + 0x0C) || on == og_dm_all_on) return;
    if (on)
        for (int c = 0; c < 256; c++) { og_dm_all_saved[c] = RD8(s + 3 * c + 0x38); RD8(s + 3 * c + 0x38) = 1; }
    else
    {
        og_log("dm: all classes restored (class 1f store level was %d)", RD8(s + 3 * 0x1F + 0x38));
        for (int c = 0; c < 256; c++) RD8(s + 3 * c + 0x38) = og_dm_all_saved[c];
    }
    og_dm_all_on = on;
}

static int og_file_exists(const char * name)
{
    uint32_t size;
    return FIO_GetFileSize(name, &size) == 0;
}

/* the counters, logged when they change */
static void og_pb_diag_watch(void)
{
    static struct og_pb_diag seen;
    struct og_pb_diag d = og_pb_diag;
    if (!memcmp(&d, &seen, sizeof(d))) return;
    seen = d;
    og_log("play: ChkFrameRate pass %d refuse %d (last %dx%d), 4-frame set-ups %d (h %d), fits %d, resize %d (last %dx%d), 2-buffer steps %d",
           d.k_pass, d.k_refuse, d.k_w, d.k_h, d.n_count, d.n_h, d.fit_count, d.d_count, d.d_w, d.d_h, d.rot_tall);
}

static void og_dm_append(const void * data, int len)
{
    if (!len || og_dm_written + len > OG_DM_MAX_FILE) return;
    FILE * f = FIO_CreateFileOrAppend(og_dm_name);
    if (!f) return;
    FIO_WriteFile(f, data, len);
    FIO_CloseFile(f);
    og_dm_written += len;
}

/* a marker record: kind 1 = PLAY entry, 2 = PLAY exit; slot = {ms, counters} */
static void og_dm_mark(int kind)
{
    uint32_t r[OG_DM_REC / 4] = { 0, kind << 16 };
    r[2] = get_ms_clock();
    memcpy(&r[3], &og_pb_diag, MIN(sizeof(og_pb_diag), OG_DM_REC - 12));
    og_dm_append(r, sizeof(r));
}

/* copy the ring lines written since the last call (the newest one again: it may have been
 * half written) into og_dm_buf, then append them */
static void og_dm_capture(void)
{
    uint32_t s = og_rd(OG_DM_PTR);
    if (!og_dm_buf || !og_ram_ok(s, OG_DM_S_LEN) || !og_rd(s + 0x0C)) return;
    int nrec = 0;
    for (int k = 0; k < 3; k++)
    {
        uint32_t h, len;
        if (!(h = og_dm_ring(s, k, &len))) continue;
        for (int sub = 0; sub < 2; sub++)
        {
            uint32_t off = RD32(h + 4 * (2 + 15 * sub)), n = RD32(h + 4 * (4 + 15 * sub));
            uint32_t base = RD32(h + 4 * (5 + 15 * sub)), cnt = RD32(h + 4 * (6 + 15 * sub));
            if (!n) continue;
            uint32_t from = og_dm_first ? (cnt > n ? cnt - n : 0) : og_dm_last[k][sub];
            if (cnt - from > n) from = cnt - n;                     /* the ring wrapped: lost */
            uint32_t room = OG_DM_BUF_RECS - nrec;
            if (cnt - from > room) from = cnt - room;
            for (uint32_t j = from; j < cnt; j++)
            {
                uint8_t * r = og_dm_buf + nrec++ * OG_DM_REC;
                ((uint32_t *) r)[0] = h;
                ((uint32_t *) r)[1] = sub;
                memcpy(r + 8, (void *) (h + off + ((j + base) % n) * OG_DM_SLOT), OG_DM_SLOT);
            }
            og_dm_last[k][sub] = cnt ? cnt - 1 : 0;
        }
    }
    og_dm_first = 0;
    og_dm_append(og_dm_buf, nrec * OG_DM_REC);
}

/* player and ORCA state words appended (record kind 3: {ms, len, words}) whenever they
 * change, every capture: what the convert and the resize were given, and the decoded
 * frames, also when Canon logs nothing at the moment that matters.
 * base = a RAM word holding the base pointer (0: off is an address) */
static const struct { uint16_t base, off, len; } og_dm_watch_list[] =
{
    { 0,      0xE978, 0x18 },   /* OrcaDS global: +0x10 state, +0x14 state flags */
    { 0xE988, 0x060,  0x10 },   /* OrcaDS: the 4 resize input words (pitch, rows, region) */
    { 0xE988, 0x1F0,  0x08 },   /* window cut */
    { 0xE988, 0x200,  0x4C },   /* convert: In type / w / h, Out pitch ..., field mode */
    { 0xE988, 0x24C,  0x20 },
    { 0x9D48, 0x9C0,  0x10 },   /* player: movie info w, h */
    { 0x9D48, 0x658,  0x4C },   /* decoded frames: count +0x670, records from +0x690 */
    { 0x9D48, 0x6A4,  0x4C },
    { 0x9D48, 0x770,  0x4C },   /* convert buffers +0x770 */
    { 0x9D48, 0x7BC,  0x10 },
    { 0x9D48, 0x908,  0x14 },   /* convert-buffer indices +0x908 .. +0x918 */
};
#define OG_DM_WATCH_N COUNT(og_dm_watch_list)
static uint32_t og_dm_watch_seen[OG_DM_WATCH_N][0x4C / 4];

static void og_dm_watch(int force)
{
    uint32_t r[OG_DM_REC / 4];
    for (int i = 0; i < (int) OG_DM_WATCH_N; i++)
    {
        uint32_t a = og_dm_watch_list[i].off, n = og_dm_watch_list[i].len;
        if (og_dm_watch_list[i].base)
        {
            uint32_t b = og_rd(og_dm_watch_list[i].base);
            if (!b) continue;
            a += b;
        }
        if (!og_ram_ok(a, n)) continue;
        if (!force && !memcmp(og_dm_watch_seen[i], (void *) a, n)) continue;
        memcpy(og_dm_watch_seen[i], (void *) a, n);
        memset(r, 0, sizeof(r));
        r[0] = a;
        r[1] = 3 << 16;
        r[2] = get_ms_clock();
        r[3] = n;
        memcpy(&r[4], og_dm_watch_seen[i], n);
        og_dm_append(r, sizeof(r));
    }
}

static void og_dm_session(void)
{
    uint32_t n = 0;
    FILE * f = FIO_OpenFile(OG_DM_SESS, O_RDONLY | O_SYNC);
    if (f)
    {
        uint32_t * b = fio_malloc(4);
        if (b && FIO_ReadFile(f, b, 4) == 4) n = *b;
        if (b) fio_free(b);
        FIO_CloseFile(f);
    }
    n++;
    f = FIO_CreateFile(OG_DM_SESS);
    if (f) { FIO_WriteFile(f, &n, 4); FIO_CloseFile(f); }
    snprintf(og_dm_name, sizeof(og_dm_name), "ML/LOGS/OGR%03d.BIN", n % 1000);
    uint32_t hdr[4] = { 0x4C52474F, 1, og_rd(OG_DM_PTR), 0 };      /* "OGRL", version, S */
    og_dm_append(hdr, sizeof(hdr));
    og_log("dm: playback ring log of this boot: %s", og_dm_name);
}

static void og_pb_diag_task(void * unused)
{
    int boot_checked = 0;
    TASK_LOOP
    {
        int play = og_debug && og_pb_in && is_play_or_qr_mode();
        msleep(play ? 20 : 200);
        if (ml_shutdown_requested) break;
        if (!og_debug) continue;
        if (!boot_checked)
        {
            boot_checked = 1;
            if (og_file_exists(OG_DM_ARM_FILE))
            {
                FIO_RemoveFile(OG_DM_ARM_FILE);
                og_log("dm: the last playback session did not end (crash or battery pull)");
            }
        }
        if (og_pb_in) og_pb_diag_watch();
        if (!play)
        {
            if (og_dm_play_since)
            {
                og_dm_play_since = 0;
                og_dm_capture();
                og_dm_mark(2);
                og_dm_all(0);
                og_log("dm: playback ended, %d KB in %s", og_dm_written >> 10, og_dm_name);
                FIO_RemoveFile(OG_DM_ARM_FILE);
            }
            continue;
        }
        int now = get_ms_clock();
        static int all_since, clips_seen;
        int clips = og_pb_diag.k_pass + og_pb_diag.k_refuse + og_pb_diag.n_count;
        if (!og_dm_play_since)
        {
            if (!og_dm_buf) og_dm_buf = malloc(OG_DM_BUF_RECS * OG_DM_REC);
            if (!og_dm_name[0]) og_dm_session();
            og_dm_play_since = now;
            FILE * f = FIO_CreateFile(OG_DM_ARM_FILE);
            if (f) FIO_CloseFile(f);
            og_dm_first = 1;
            og_dm_mark(1);
            og_dm_watch(1);
            clips_seen = -1;
            og_log("dm: playback started");
        }
        /* every class at level 1 for 10 s from the PLAY entry and from each new tall clip
         * (a ChkFrameRate or frame set-up count change) */
        if (clips != clips_seen)
        {
            clips_seen = clips;
            all_since = now;
            og_dm_all(1);
        }
        if (now - all_since > OG_DM_ALL_MS) og_dm_all(0);
        if (now - og_dm_play_since <= OG_DM_TIME) { og_dm_capture(); og_dm_watch(0); }
    }
}

/* ------------------------------------------------------------------------
 * property handlers: only set flags; a removal on a setting change finishes before Canon
 * starts the new movie mode (DESIGN.md 3.3): at once outside the movie LV, with the sensor
 * off (LV conductor suspended) while the movie LV runs with the patches
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
static volatile int og_lost_keep;       /* the setting is still known (only the patches go) */
static const char * volatile og_lost_why = "";
static volatile int og_removed_notice;  /* removal to show to the user (worker) */

/* how and when each removal happened, for the log. The removal itself is never held: the
 * hold of 34496ac10 (wait for the sensor stop) froze the camera on a Q-menu 25p -> 50p
 * change, which switches the capture mode over the running LV right after the posting */
static volatile int og_lost_since;      /* when the pending removal was first noted */
static volatile int og_lv_stopping;     /* the LV_ACTION handler is removing at Canon's LV stop */
static const char * volatile og_removed_how = "";
static volatile int og_removed_after;   /* ms from the note to the removal */

static int og_movie_lv_running(void)
{
    return lv && og_path() == OG_PATH_MOVIE && !og_lv_suspended();
}

/* the caller holds og_lock */
static void og_handle_lost_locked(void)
{
    if (!og_lost_pending || RECORDING) return;
    int waited = get_ms_clock() - og_lost_since;
    /* no hold: a Q-menu change (4K 25p -> 50p) switches the capture mode over the running LV
     * 30 ms after the posting, without a stop; held, the patches were still in and the camera
     * froze (clean-qmenu). At once, in the handler, as before 34496ac10 (run 57: removed before
     * Canon's restart, twice) */
    og_removed_how = og_lv_stopping ? "at PROP_LV_ACTION stop"
                   : og_movie_lv_running() ? "LV stop not seen, removed under the LV"
                   : og_lv_suspended() ? "LV suspended (sensor off)" : "LV not running";
    og_removed_after = waited;
    og_lost_pending = 0;
    if (!og_lost_keep) og_setting_col = 0;
    og_lost_keep = 0;
    if (!og_removal_needed()) return;
    og_remove();
    og_reason = og_lost_why;
    og_state = og_ui ? OG_ARMED : OG_OFF;
    og_removed_notice = 1;
}

/* records a lost setting: the worker (og_lock held) removes in this step or the next.
 * keep: the open-gate setting stays known (another movie setting changed), the next safe
 * point re-applies */
static void og_lost_note(const char * why, int keep)
{
    if (!keep) og_setting_col = 0;
    if (!og_lost_pending) og_lost_since = get_ms_clock();
    og_lost_keep = keep && !og_lost_pending;
    og_lost_why = why;
    og_lost_pending = 1;
}

/* the four line-time words of slot 9 back to Canon's values, without the lock: only words that
 * hold ours are written, so the later full removal finds them stock and skips them */
static void og_hd_words_stock(void)
{
    for (int i = 0; i < COUNT(og_sensor_cells); i++)
        for (int k = 0; k < COUNT(og_hd_words); k++)
            if (og_sensor_cells[i].addr == og_hd_words[k] && RD32(og_hd_words[k]) == og_sensor_cells[i].val)
                RD32(og_hd_words[k]) = og_sensor_cells[i].stock;
}

/* from a property handler (task context): remove at once if possible */
static void og_lost(const char * why, int keep)
{
    og_lost_note(why, keep);
    if (RECORDING) return;                          /* the worker removes after the stop */
    if (take_semaphore(og_lock, 1000) != 0)
    {
        /* the worker removes on its next step; if the new setting is one the open gate does
         * not take (e.g. 4K 50/59.94p, the same sensor slot 9), the line-time words go back
         * now, so that mode never starts with HD-576 words on its HD-524 cells */
        if (!keep && og_removal_needed()) og_hd_words_stock();
        return;
    }
    og_handle_lost_locked();
    give_semaphore(og_lock);
}

static void og_setting_lost(const char * why)
{
    og_lost(why, 0);
}

/* Canon's movie setting: PROP_MOVIE_CONTROL_PARAM and PROP_MOVIE_CROP_MODE (src/property.h;
 * delivered by an ML core with CONFIG_PROP_MOVIE_CONTROL_PARAM, R6 has no PROP_VIDEO_MODE).
 * With an older core neither arrives and only the live checks remain. */
/* only what decides: byte 0x30 (0 NTSC / 1 PAL rates, 2 = 24.00p) is not kept, 24.00p has
 * its own exact rate (2400, not supported); Canon re-posts at every photo / movie switch,
 * which must not count as a change. Leaving the movie LV posts the photo LV setting, not
 * the movie one: 29.97p with any NTSC movie rate (camera log, 4K 23.98p), so og_mcp is
 * only taken from the postings of the movie setting (og_decide_posting) */
struct og_mcp { uint32_t fps100, size, readout; };
static struct og_mcp og_mcp;            /* the movie setting */
static volatile int og_mcp_calls;
static volatile int og_crop;            /* movie crop on */
static volatile int og_setting_changes; /* counts posted changes of the setting */

#define OG_MCP_SIZE_UHD     5
#define OG_MCP_READOUT_FULL 5

static const struct og_rate * og_rate_of_col(int col)
{
    for (int i = 0; i < COUNT(og_rates); i++)
        if (og_rates[i].col == col) return &og_rates[i];
    return 0;
}

/* the timing column of a supported setting, or 0 with the reason */
static int og_mcp_col(const struct og_mcp * m, const char ** why)
{
    const char * w = "";
    int col = 0;
    if (m->size != OG_MCP_SIZE_UHD) w = "Movie size is not 4K";
    else if (m->readout != OG_MCP_READOUT_FULL) w = "Movie crop on";
    else
    {
        for (int i = 0; i < COUNT(og_rates); i++)
            if (og_rates[i].fps100 == m->fps100) col = og_rates[i].col;
        if (!col) w = "Frame rate not supported";
    }
    if (why) *why = w;
    return col;
}

/* the same for a mode (both modes take all three rates since the HD 576 line time). The mode
 * is the one of the patches in place, or the user's choice while none are */
static int og_mode_now(void)
{
    return og_removal_needed() ? og_mode : og_ui;
}

static int og_mcp_col_mode(const struct og_mcp * m, const char ** why, int mode)
{
    return og_mcp_col(m, why);
}

/* debug: when each posting comes, relative to Canon's LiveView state (is the setting
 * posted before the movie LV restarts?). Written by the handlers, logged by the worker */
struct og_ev { int ms; uint32_t prop, a, b, c, d, dlo, dhi; int cap, slot, path, state; };
#define OG_EV_N 16
static struct og_ev og_ev[OG_EV_N];
static volatile int og_ev_w, og_ev_r;

static void og_event(uint32_t prop, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t dlo, uint32_t dhi)
{
    if (!og_debug || og_ev_w - og_ev_r >= OG_EV_N) return;
    og_ev[og_ev_w % OG_EV_N] = (struct og_ev) { get_ms_clock(), prop, a, b, c, d, dlo, dhi,
                                                og_capmode(), og_slot(), og_path(), og_state };
    og_ev_w++;
}

/* the whole posting of the movie setting: any change other than the frame rate (words 0
 * and 1, byte 0x30 PAL / NTSC) is a new recording setup for Canon (codec, bit depth, GOP,
 * quality...). Switching Canon Log on (H.264 8-bit -> HEVC 10-bit) with the patches ACTIVE
 * gave Err 70 (camera log): such a change removes the patches, and the next safe point
 * re-applies */
static uint8_t og_mcp_raw[PROP_MOVIE_CONTROL_PARAM_LEN];
static int og_mcp_raw_ok;
#define OG_MCP_FPS_LO  0x000000FFu      /* bytes 0-7: w0, w1 */
#define OG_MCP_FPS_HI  (1u << (0x30 - 32))

/* the photo LV posting seen with the current movie setting (a boot in photo mode may give
 * the core only this one) */
static struct og_mcp og_photo_mcp;
static volatile int og_photo_ok;

/* the last posting, decided by the worker: a photo / movie switch posts it and then, 20-50
 * ms later (camera logs), PROP_MOVIE_CROP_MODE; a menu change posts it alone. The handler
 * writes it between two increments of og_pend_seq (odd while writing) */
static struct og_mcp og_pend;
static uint8_t og_pend_raw[PROP_MOVIE_CONTROL_PARAM_LEN];
static volatile int og_pend_since;      /* time of a posting still to decide, 0 none */
static volatile int og_pend_path;       /* Canon's LV path when it came: the mode being left */
static volatile int og_pend_switch;     /* PROP_MOVIE_CROP_MODE followed: photo / movie switch */
static volatile int og_pend_seq;
/* the Err 70 of a change kept under the patches came seconds later, at record start, and
 * Canon restarts the LV about 2 s after the posting. A switch whose crop posting does not
 * come in time only costs a removal (the safe side) */
#define OG_PEND_WAIT_MS 500

/* bytes of a posting that differ from the movie setting, other than the frame rate */
static int og_mcp_other(const uint8_t * b, uint32_t * dlo_out, uint32_t * dhi_out)
{
    uint32_t dlo = 0, dhi = 0;
    if (og_mcp_raw_ok)
        for (int i = 0; i < PROP_MOVIE_CONTROL_PARAM_LEN; i++)
            if (b[i] != og_mcp_raw[i]) { if (i < 32) dlo |= 1u << i; else dhi |= 1u << (i - 32); }
    if (dlo_out) *dlo_out = dlo;
    if (dhi_out) *dhi_out = dhi;
    return (dlo & ~OG_MCP_FPS_LO) || (dhi & ~OG_MCP_FPS_HI);
}

PROP_HANDLER(PROP_MOVIE_CONTROL_PARAM)
{
    og_in_handler = 1;
    if (len >= PROP_MOVIE_CONTROL_PARAM_LEN)
    {
        const uint8_t * b = (const uint8_t *) buf;
        struct og_mcp m = { buf[1], b[8], b[0x2C] };
        uint32_t dlo, dhi;
        og_mcp_other(b, &dlo, &dhi);
        og_event(PROP_MOVIE_CONTROL_PARAM, m.fps100, m.size, m.readout, b[0x30], dlo, dhi);
        og_pend_seq++;
        og_pend = m;
        memcpy(og_pend_raw, b, PROP_MOVIE_CONTROL_PARAM_LEN);
        og_pend_path = og_path();
        og_pend_switch = 0;
        og_pend_since = get_ms_clock() | 1;
        og_pend_seq++;
        /* not supported: the patches go now, before Canon restarts the LV. The setting
         * stays known until the posting is decided (the photo LV one is not the setting) */
        const char * why;
        if (!og_mcp_col_mode(&m, &why, og_mode_now()) && og_removal_needed()) og_lost(why, 1);
        /* a recording setup change (same rate, other bytes) posted while the movie LV is
         * already stopped (Canon's MENU opened from the movie LV or during REC): the patches
         * go now, with the sensor off. Decided 500 ms later the LV has restarted with the new
         * codec and the removal would be held, then made under the running LV: that froze the
         * camera (clean-run1, 2026-11-24). A photo / movie switch posts while its LV still
         * runs, so it never takes this path */
        else if (og_removal_needed() && !og_movie_lv_running() && og_pend_path == OG_PATH_MOVIE
                 && og_mcp_raw_ok && m.fps100 == og_mcp.fps100 && og_mcp_other(b, 0, 0))
            og_lost("Movie recording settings changed", 1);
    }
    og_in_handler = 0;
}

PROP_HANDLER(PROP_MOVIE_CROP_MODE)
{
    og_in_handler = 1;
    og_event(PROP_MOVIE_CROP_MODE, buf[0], 0, 0, 0, 0, 0);
    if (og_pend_since) og_pend_switch = 1;
    int c = buf[0] != 0;
    if (c != og_crop) og_setting_changes++;
    og_crop = c;
    if (c) og_setting_lost("Movie crop on");
    og_in_handler = 0;
}

PROP_HANDLER(PROP_HDMI_CHANGE)
{
    og_in_handler = 1;
    og_event(PROP_HDMI_CHANGE, buf[0], 0, 0, 0, 0, 0);
    og_hdmi = buf[0];
    if (og_hdmi) og_setting_lost("HDMI connected");
    og_in_handler = 0;
}

/* Canon's LV start (0) / stop (1), posted by the GUI (menus): a moment for a held removal,
 * the old movie LV is going away and the new one is not set up yet */
/* R6 ids (src/property.h, CONFIG_R6_190, not defined for module builds) */
#ifndef PROP_TEMP_DRAM
#define PROP_TEMP_DRAM          0x80030035
#define PROP_TEMP_MIMI_L        0x800300C6
#define PROP_TEMP_MIMI_R        0x8003008D
#define PROP_TEMP_A             0x8003009E
#define PROP_TEMP_TFT           0x80030081
#define PROP_TEMP_SH            0x80030062
#define PROP_TEMP_STATUS        0x8004000F
#endif
extern int get_heat_budget_percent(void);       /* src/debug.c, -1 unknown */
extern int get_heat_remain_seconds(void);       /* src/debug.c, -1 unknown */

/* environment for the debug log (log only): Canon's TempSvc temperatures (raw = deg C + 128,
 * 0 before the first reading, src/property.h), the overheat level and the battery level, as
 * Canon posts them; the heat budget and heat timer come from the core's cached values. Nothing
 * is requested */
static const uint32_t og_temp_props[6] = { PROP_TEMP_DRAM, PROP_TEMP_MIMI_L, PROP_TEMP_MIMI_R,
                                           PROP_TEMP_A, PROP_TEMP_TFT, PROP_TEMP_SH };
static volatile uint32_t og_temp_raw[6];
static volatile int og_temp_status = -1;
static volatile int og_battery = -1;

static void og_temp_store(unsigned property, uint32_t v)
{
    for (int i = 0; i < 6; i++) if (og_temp_props[i] == property) og_temp_raw[i] = v;
}

PROP_HANDLER(PROP_TEMP_DRAM)   { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_MIMI_L) { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_MIMI_R) { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_A)      { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_TFT)    { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_SH)     { og_temp_store(property, buf[0]); }
PROP_HANDLER(PROP_TEMP_STATUS) { og_temp_status = buf[0]; }
PROP_HANDLER(PROP_BATTERY_REPORT) { if (len >= 8) og_battery = buf[1] & 0xFF; }

static int og_temp_c(int i)
{
    uint32_t r = og_temp_raw[i];
    return r ? (int) r - 128 : -999;
}

/* the worker only (file access) */
static void og_env_log(const char * when)
{
    /* no "%%": Canon's vsnprintf eats an argument on it */
    og_log("env (%s): temp DRAM %d MIMI-L %d MIMI-R %d A %d TFT %d SH %d C, overheat %d, "
           "heat budget %d pct, heat timer %d s, battery %d pct",
           when, og_temp_c(0), og_temp_c(1), og_temp_c(2), og_temp_c(3), og_temp_c(4), og_temp_c(5),
           og_temp_status, get_heat_budget_percent(), get_heat_remain_seconds(), og_battery);
}

PROP_HANDLER(PROP_LV_ACTION)
{
    og_in_handler = 1;
    og_event(PROP_LV_ACTION, buf[0], og_lost_pending, og_lv_suspended(), 0, 0, 0);
    if (buf[0] == 1 && og_removal_needed() && og_lost_pending && !RECORDING
        && take_semaphore(og_lock, 1000) == 0)
    {
        og_lv_stopping = 1;
        og_handle_lost_locked();
        og_lv_stopping = 0;
        give_semaphore(og_lock);
    }
    og_in_handler = 0;
}

/* ------------------------------------------------------------------------
 * worker task: the only place that applies; checks the live state
 * ------------------------------------------------------------------------ */

/* Canon's setting must be the rate of the column: column 4 is also 24.00p. Without the
 * property (older ML core) only the live checks remain (logged by og_debug_step) */
static int og_prop_ok(int col)
{
    if (!og_mcp_calls) return 1;
    struct og_mcp m = og_mcp;
    return !og_crop && og_mcp_col(&m, 0) == col;
}

/* the learnt setting, saved for the next boot with the property values seen with it
 * (vm0 fps x 100, vm1 movie size, vm2 readout) */
static CONFIG_INT("opengate.col", og_saved_col, 0);
static CONFIG_INT("opengate.vm0", og_saved_vm0, 0);
static CONFIG_INT("opengate.vm1", og_saved_vm1, 0);
static CONFIG_INT("opengate.vm2", og_saved_vm2, 0);
static CONFIG_INT("opengate.vm.ok", og_saved_vm_ok, 0);
/* the photo LV posting seen with it (pvm0-2 as vm0-2) */
static CONFIG_INT("opengate.pvm0", og_saved_pvm0, 0);
static CONFIG_INT("opengate.pvm1", og_saved_pvm1, 0);
static CONFIG_INT("opengate.pvm2", og_saved_pvm2, 0);
static CONFIG_INT("opengate.pvm.ok", og_saved_pvm_ok, 0);

/* written only when something changes (the config is saved at power off) */
static void og_save_setting(int col)
{
    struct og_mcp m = og_mcp, p = og_photo_mcp;
    int ok = col && og_mcp_calls > 0;
    int vm0 = ok ? (int) m.fps100 : 0, vm1 = ok ? (int) m.size : 0, vm2 = ok ? (int) m.readout : 0;
    int pok = ok && og_photo_ok;
    int pvm0 = pok ? (int) p.fps100 : 0, pvm1 = pok ? (int) p.size : 0, pvm2 = pok ? (int) p.readout : 0;
    if (og_saved_col == col && og_saved_vm_ok == ok && og_saved_vm0 == vm0 && og_saved_vm1 == vm1 && og_saved_vm2 == vm2
        && og_saved_pvm_ok == pok && og_saved_pvm0 == pvm0 && og_saved_pvm1 == pvm1 && og_saved_pvm2 == pvm2)
        return;
    og_saved_col = col;
    og_saved_vm_ok = ok;
    og_saved_vm0 = vm0;
    og_saved_vm1 = vm1;
    og_saved_vm2 = vm2;
    og_saved_pvm_ok = pok;
    og_saved_pvm0 = pvm0;
    og_saved_pvm1 = pvm1;
    og_saved_pvm2 = pvm2;
}

/* learn the setting from Canon's own (unpatched) movie LV. A sample counts only when the
 * same result is seen OG_STABLE times in a row: while the movie LV starts or stops Canon
 * shows capture mode 0 and stale slots (log of the first clean-module test) */
#define OG_STABLE 5
static void og_observe_setting(void)
{
    static int cand_bad = -1, cand_col, cand_n;
    if (!lv || og_path() != OG_PATH_MOVIE || og_removal_needed()) { cand_n = 0; return; }
    int seq = og_setting_changes;
    int cap = og_capmode(), slot = og_slot(), col = og_rd(OG_TIMING_INFO + 0x24);
    if (cap == 0) { cand_n = 0; return; }   /* no capture mode: LV starting or stopping */
    int bad = 0;
    if (cap == OG_CAPMODE_4KCROP || og_crop) bad = 2;
    else if (cap != OG_CAPMODE_4K30 || slot != OG_SLOT_4K30 || !og_rate_of_col(col)) bad = 1;
    else if (!og_prop_ok(col)) bad = 3;
    if (seq != og_setting_changes) { cand_n = 0; return; }   /* the setting changed while reading */
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
            og_log("setting not usable (%d): capmode %x slot %d fps idx %d fps %d size %d readout %d crop %d",
                   bad, cap, slot, col, og_mcp.fps100, og_mcp.size, og_mcp.readout, og_crop);
        og_setting_col = 0;
        og_save_setting(0);
    }
    og_setting_bad = bad;
}

/* after a boot in photo mode: reuse the setting of the last session, only if Canon's
 * movie setting (posted at boot) is the one seen with it. Movie settings change only in
 * movie mode, where the module observes them */
/* Canon posts the setting once at boot, before modules are loaded: the core keeps it
 * (src/propvalues.c). Taken once, if no posting reached the module yet */
static void og_seed_from_core(void)
{
    static int done;
    if (done || og_mcp_calls) return;
    if (video_mode_fps_x100 <= 0 || video_mode_size_code < 0) return;
    done = 1;
    og_crop = video_mode_movie_crop;
    /* the core does not keep the readout: movie crop gives it (camera log: 4 with crop) */
    og_mcp = (struct og_mcp) { video_mode_fps_x100, video_mode_size_code,
                               og_crop ? 4 : OG_MCP_READOUT_FULL };
    og_mcp_calls = 1;
    og_log("setting from the core at boot: fps %d size %d crop %d", og_mcp.fps100, og_mcp.size, og_crop);
}

static void og_recall_setting(void)
{
    static int done;
    og_seed_from_core();
    if (done || !og_mcp_calls) return;   /* once, after the boot delivery of the property */
    done = 1;
    if (og_setting_col) return;
    const struct og_rate * r = og_rate_of_col(og_saved_col);
    if (!r) return;
    struct og_mcp m = og_mcp;
    int same = og_saved_vm_ok && og_saved_vm0 == (int) m.fps100
            && og_saved_vm1 == (int) m.size && og_saved_vm2 == (int) m.readout;
    /* a boot in photo mode: the core may hold the photo LV posting (29.97p with an NTSC
     * movie rate). Then the movie setting is the saved one; the movie posting before the LV
     * restarts corrects it, and all three columns are patched */
    int photo = !same && og_saved_vm_ok && og_saved_pvm_ok && og_saved_pvm0 == (int) m.fps100
            && og_saved_pvm1 == (int) m.size && og_saved_pvm2 == (int) m.readout;
    og_log("last session: 4K %s, movie setting %s", r->name,
           same ? "same: reused" : photo ? "same photo LV posting: reused" : "different: not reused");
    if (!same && !photo) return;
    if (photo) og_mcp = (struct og_mcp) { og_saved_vm0, og_saved_vm1, og_saved_vm2 };
    if (og_saved_pvm_ok && !og_photo_ok)
    {
        og_photo_mcp = (struct og_mcp) { og_saved_pvm0, og_saved_pvm1, og_saved_pvm2 };
        og_photo_ok = 1;
    }
    if (!og_prop_ok(r->col)) return;
    og_setting_col = r->col;
    og_setting_seq = og_setting_changes;
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
    const struct og_rate * live = og_rate_of_col(og_rd(OG_TIMING_INFO + 0x24));
    int ok = cap == OG_CAPMODE_4K30 && slot == OG_SLOT_FULL && live
          && vd >= live->fl && vd <= live->fl + 2;
    if (ok)
    {
        if (og_rate != live) og_log("live rate: 4K %s", live->name);
        og_rate = live;     /* the rate shown in the menu */
        /* a rate changed under the patches (25p <-> 29.97p from Canon's MENU) is the setting
         * now: the same checks as og_observe_setting (capture mode, slot, live column, and
         * Canon's posted setting), plus the VD of that column. Without this the setting stayed
         * the old rate with a newer change count, and after the removal at the next photo
         * switch the apply waited for a stock movie LV to learn it again (two photo visits) */
        int seq = og_setting_changes;
        if ((og_setting_col != live->col || og_setting_seq != seq) && !og_pend_since
            && og_prop_ok(live->col) && seq == og_setting_changes)
        {
            if (og_setting_col != live->col) og_log("setting: 4K %s (live)", live->name);
            og_setting_col = live->col;
            og_setting_seq = seq;
            og_setting_bad = 0;
            og_save_setting(live->col);
        }
        if (og_bad_samples) og_log("live state back after %d bad samples", og_bad_samples);
        og_bad_samples = 0;
        if (og_state == OG_APPLIED)
        {
            og_state = OG_ACTIVE;
            og_log("active: capmode %x slot %d VD %d, base sync %d, Canon 24.00p setting %d",
                   cap, slot, vd, og_rd(OG_TIMING_INFO + 4), og_24p_stored());
            /* the SetP / cartridge cells of the live column and Canon's live cartridge copy
             * (*(0x11EA8+0x34), set at LV setup): all must be ours (OG_T_SETP / OG_T_CART) */
            int lc = og_rd(OG_TIMING_INFO + 0x24);
            og_log("timing in effect (col %d): SetP %d, cartridge %x, live cartridge %x",
                   lc, RD16(OG_TCELL(0x280, lc)), RD32(OG_TCART(lc)), og_rd(OG_TIMING_INFO + 0x34));
            /* line time: the HD cell, Canon's live HD us copy, and the DIGIC timing registers
             * read back (HD - 1 / HD = 23f / 240; the CMOS side cannot be read back) */
            og_log("line time: HD cell %d, live HD us %d, D2408198 %x, D240819C %x",
                   RD16(OG_TCELL(0xA00, lc)), og_rd(OG_TIMING_INFO + 0x30), RD32(0xD2408198), RD32(0xD240819C));
            NotifyBox(2000, "Open gate ON: %s %s", og_mode_size[og_mode], og_rate->name);
        }
        return;
    }
    /* logged from the 3rd sample: the LV teardown at power off shows one or two samples of
     * capture mode 0, and nothing should be written to the card then */
    if (++og_bad_samples == 3)
        og_log("live state wrong: capmode %x slot %d VD %d fps idx %d", cap, slot, vd, og_rd(OG_TIMING_INFO + 0x24));
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

#define OG_READY_MS 2500
static int og_ready_shown;     /* time the "ready" box was shown: hidden on leaving photo mode */

static void og_debug_step(void)
{
    if (!og_debug) return;
    og_assert_hook_install();
    if (og_assert_hit == 1)
    {
        og_assert_hit = 2;
        og_log("%s", og_assert_buf);
        og_env_log("assert");
        /* what Canon logged before it: the camera still runs the worker here (camera log) */
        if (!ml_shutdown_requested)
            og_log("Canon ring to OGASSERT.BIN: %d blocks", og_dm_dump("ML/LOGS/OGASSERT.BIN"));
    }
    static int env_boot;
    if (!env_boot && (og_temp_raw[0] || get_ms_clock() > 10000)) { env_boot = 1; og_env_log("boot"); }
    og_assert_ctx[0] = og_state;
    og_assert_ctx[1] = lv;
    og_assert_ctx[2] = og_path();
    og_assert_ctx[3] = og_capmode();

    /* property postings, with the LiveView state seen by the handler and now */
    while (og_ev_r != og_ev_w)
    {
        struct og_ev e = og_ev[og_ev_r % OG_EV_N];
        og_ev_r++;
        og_log("prop %x at %d: %d %d %d %d (changed bytes mask %05x%08x); then capmode %x slot %d path %x state %d; now capmode %x slot %d path %x",
               e.prop, e.ms, e.a, e.b, e.c, e.d, e.dhi, e.dlo, e.cap, e.slot, e.path, e.state, og_capmode(), og_slot(), og_path());
    }
    static int logged_calls;
    if (!og_mcp_calls && !logged_calls && get_ms_clock() > 15000)
    {
        logged_calls = 1;
        og_log("PROP_MOVIE_CONTROL_PARAM not delivered (ML core without it?)");
    }
}

/* the last PROP_MOVIE_CONTROL_PARAM posting (handler above), once the crop posting came or
 * OG_PEND_WAIT_MS passed: leaving the movie LV posts the photo LV setting, kept apart;
 * entering it and a menu change post the movie setting */
static void og_decide_posting(void)
{
    int since = og_pend_since, seq = og_pend_seq;
    if (!since || (seq & 1)) return;
    int switched = og_pend_switch;
    if (!switched && get_ms_clock() - since < OG_PEND_WAIT_MS) return;
    struct og_mcp m = og_pend;
    int path = og_pend_path;
    uint8_t raw[PROP_MOVIE_CONTROL_PARAM_LEN];
    memcpy(raw, og_pend_raw, PROP_MOVIE_CONTROL_PARAM_LEN);
    if (og_pend_seq != seq) return;     /* a new posting came while reading */
    og_pend_since = 0;

    if (switched && path == OG_PATH_MOVIE)
    {
        if (!og_photo_ok || memcmp(&m, &og_photo_mcp, sizeof(m)) != 0)
            og_log("photo LV posting: fps %d size %d readout %d (not the movie setting)",
                   m.fps100, m.size, m.readout);
        og_photo_mcp = m;
        og_photo_ok = 1;
        if (og_setting_col) og_save_setting(og_setting_col);
        return;
    }

    int other = og_mcp_other(raw, 0, 0);
    int changed = !og_mcp_calls || memcmp(&m, &og_mcp, sizeof(m)) != 0;
    og_mcp = m;
    memcpy(og_mcp_raw, raw, PROP_MOVIE_CONTROL_PARAM_LEN);
    og_mcp_raw_ok = 1;
    og_mcp_calls++;
    if (changed)
    {
        og_setting_changes++;
        og_photo_ok = 0;
        og_log("movie setting: fps %d size %d readout %d", m.fps100, m.size, m.readout);
        /* a supported rate alone stays applied: all three columns are patched, and the
         * live check follows the live one */
        const char * why;
        if (!og_mcp_col_mode(&m, &why, og_mode_now())) og_lost_note(why, 0);
        /* a new rate also moves rate-derived bytes (0x10-0x11, 0x1C-0x1D from 25p to
         * 29.97p, camera log): only a change with the same rate is a new recording setup */
        return;
    }
    if (!other) return;
    if (switched) { og_log("recording setup differs with a photo / movie switch: kept"); return; }
    if (og_removal_needed()) og_lost_note("Movie recording settings changed", 1);
}

/* debug log: one line at REC start and one at the stop, with the open gate on */
static void og_rec_log(void)
{
    static int rec_since;
    if (RECORDING && !rec_since && og_state == OG_ACTIVE)
    {
        rec_since = get_ms_clock() | 1;
        og_log("REC start: %s 4K %s", og_mode_size[og_mode], og_rate ? og_rate->name : "");
    }
    else if (!RECORDING && rec_since)
    {
        og_log("REC stop after %d s, state %d", (get_ms_clock() - rec_since) / 1000, og_state);
        rec_since = 0;
    }
}

static void og_step(void)
{
    og_debug_step();
    og_rec_log();
    og_decide_posting();
    if (og_ready_shown)
    {
        if (get_ms_clock() - og_ready_shown >= OG_READY_MS) og_ready_shown = 0;
        else if (og_path() != OG_PATH_PHOTO) { og_ready_shown = 0; NotifyBoxHide(); }
    }
    og_handle_lost_locked();
    og_recall_setting();
    if (og_removed_notice)
    {
        og_removed_notice = 0;
        og_log("removed by a setting change: %s (%s, %d ms after the change, %d words left)",
               og_reason, og_removed_how, og_removed_after, og_last_left);
        NotifyBox(4000, "Open gate turned off:\n%s", og_reason);
    }
    og_observe_setting();
    og_finish_deferred();
    og_pb_step();

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
                    og_fps24_in() ? "fps24 module on" :
                    !lv ? "no LV" : !og_safe_point() ? "not a safe point" :
                    og_pend_since ? "setting posted" :
                    !r ? "setting unknown" : og_setting_seq != og_setting_changes ? "setting changed" :
                    !og_prop_ok(col) ? "movie setting" : 0;
                static const char * logged_wait;
                if (wait != logged_wait)
                {
                    logged_wait = wait;
                    if (wait) og_log("waiting: %s (path %x, col %d)", wait, og_path(), col);
                }
                /* in the movie LV with a known, supported setting only the photo-mode visit is
                 * missing (a safe point: no 4K movie LV, no 4K memory map): say so, once each
                 * time this is reached */
                static int hint_shown;
                int need_photo = r && lv && og_path() == OG_PATH_MOVIE && !RECORDING
                    && (wait == 0 || !strcmp(wait, "not a safe point") || !strcmp(wait, "deferred restore"))
                    && og_setting_seq == og_setting_changes && og_prop_ok(col)
                    && !og_hdmi && !og_pend_since && !og_fps24_in();
                if (!need_photo) hint_shown = 0;
                else if (!hint_shown)
                {
                    hint_shown = 1;
                    og_log("hint: photo mode once to apply %s %s", og_mode_size[og_ui], r->name);
                    if (og_reason && og_reason[0])
                        NotifyBox(6000, "Open gate off: %s.\nSwitch to photo mode once to apply 4K %s.", og_reason, r->name);
                    else
                        NotifyBox(5000, "Open gate: switch to photo mode once\nto apply 4K %s.", r->name);
                }
                if (wait) break;
                if (!og_alloc_copies()) { og_reason = "Not enough memory"; og_ui = 0; og_state = OG_OFF; break; }
                int mode = og_ui;
                if (!og_prepare(r, mode) || !og_commit())
                {
                    og_log("apply refused: %s", og_reason);
                    NotifyBox(5000, "Open gate refused:\n%s", og_reason);
                    og_ui = 0;
                    og_state = OG_OFF;
                    break;
                }
                og_mode = mode;
                og_rate = r;
                og_active_since = 0;
                og_bad_samples = 0;
                og_state = OG_APPLIED;
                og_log("applied: %s 4K %s, Canon 24.00p setting %d", og_mode_size[mode], r->name, og_24p_stored());
                og_env_log("apply");
                NotifyBox(OG_READY_MS, "Open gate ready (%s %s):\nswitch to movie mode.", og_mode_size[mode], r->name);
                og_ready_shown = get_ms_clock() | 1;
            }
            break;

        /* OFF or the other size in the menu: removed at the next safe point; the other size is
         * then applied by OFF -> ARMED (no reboot) */
        case OG_APPLIED:
        case OG_ACTIVE:
            if (og_ui != og_mode) { og_state = OG_REMOVING; break; }
            og_check_live();
            break;

        case OG_REMOVING:
            if (og_ui == og_mode) { og_state = OG_APPLIED; og_active_since = 0; break; }
            if (og_safe_point())
            {
                og_remove();
                og_state = OG_OFF;
                og_reason = "";
                og_log(og_ui ? "removed (menu: other size)" : "removed (menu OFF)");
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
            MENU_SET_RINFO("%s %s", og_mode_size[og_mode], og_rate ? og_rate->name : "");
            break;
        case OG_APPLIED:
            MENU_SET_VALUE("Ready");
            MENU_SET_RINFO("%s", og_mode_size[og_mode]);
            MENU_SET_WARNING(MENU_WARN_INFO, "Ready: switch to movie mode (4K %s).", og_rate ? og_rate->name : "");
            break;
        case OG_REMOVING:
            MENU_SET_VALUE(og_ui ? "Changing" : "Turning off");
            MENU_SET_WARNING(MENU_WARN_INFO, og_ui ? "Switch to photo mode once to change the size."
                                                   : "Switch to photo mode once to finish turning off.");
            break;
        case OG_ARMED:
        {
            MENU_SET_VALUE("Waiting");
            MENU_SET_RINFO("%s", og_mode_size[og_ui]);
            if (og_hdmi)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Disconnect HDMI.");
            else if (og_fps24_in())
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Turn 24.00p (fps24) OFF first.");
            else if (og_setting_bad == 2)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Turn Canon movie cropping OFF.");
            else if (og_setting_bad == 3)
                MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Use 4K 25p, 29.97p or 23.98p.");
            else if (og_setting_bad || !og_setting_col)
                MENU_SET_WARNING(MENU_WARN_ADVICE, "Set Canon movie size to 4K 25/29.97/23.98p, crop OFF.");
            else
                MENU_SET_WARNING(MENU_WARN_ADVICE, "Switch to photo mode once to apply.");
            break;
        }
        default:
            if (og_reason && og_reason[0]) MENU_SET_WARNING(MENU_WARN_INFO, "Last: %s", og_reason);
            break;
    }
    if (RECORDING && og_state != OG_OFF)
        MENU_SET_WARNING(MENU_WARN_INFO, "Recording: changes wait until it stops.");
}

static MENU_UPDATE_FUNC(og_pb_update)
{
    if (!og_pb_ui != !og_pb_in)
        MENU_SET_WARNING(MENU_WARN_INFO, is_play_or_qr_mode() ? "Takes effect when you leave playback."
                                                              : "Takes effect when recording stops.");
    else if (!og_pb_ui && og_pb_why[0])
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Turned off: %s", og_pb_why);
}

static struct menu_entry og_menu[] =
{
    {
        .name    = "Open gate 4K",
        .priv    = &og_ui,
        .max     = 2,
        .choices = CHOICES("OFF", "3:2 4096x2736", "4:3 3840x2880"),
        .update  = og_update,
        .help    = "Full sensor height in 4K: 3:2 4096x2736 or 4:3 3840x2880.",
        .help2   = "4K 25/29.97/23.98p, crop OFF; then photo mode once.",
        .submenu_width = 650,
        .children = (struct menu_entry[]) {
            {
                .name   = "Playback",
                .priv   = &og_pb_ui,
                .max    = 1,
                .update = og_pb_update,
                .help   = "Play open gate clips on the camera, the whole picture.",
                .help2  = "Works with the open gate mode OFF too. Change it outside playback.",
            },
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
    og_log_lock = create_named_semaphore("opengate_log", 1);
    menu_add("Movie", og_menu, COUNT(og_menu));
    task_create("opengate_task", 0x1a, 0x3000, og_task, 0);
    task_create("opengate_pbdiag", 0x1a, 0x2000, og_pb_diag_task, 0);
    return 0;
}

static unsigned int og_deinit(void)
{
    /* called at power off: touch nothing. Removing the patches there ran inside the
     * running movie LV while Canon shut down and hung the power off (run 53). Every
     * change is in RAM and is rebuilt stock at the next boot. Otherwise remove only at
     * a safe point. */
    if (ml_shutdown_requested) return 0;
    if (take_semaphore(og_lock, 1000) == 0)
    {
        if (og_removal_needed() && og_safe_point()) og_remove();
        if (og_pb_in && og_pb_can_change()) og_pb_remove();
        give_semaphore(og_lock);
    }
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(og_init)
    MODULE_DEINIT(og_deinit)
MODULE_INFO_END()

/* saved by ML in ML/SETTINGS when the menu closes; the open gate entry itself is not saved
 * (OFF at boot), the playback entry is */
MODULE_CONFIGS_START()
    MODULE_CONFIG(og_debug)
    MODULE_CONFIG(og_pb_ui)
    MODULE_CONFIG(og_saved_col)
    MODULE_CONFIG(og_saved_vm0)
    MODULE_CONFIG(og_saved_vm1)
    MODULE_CONFIG(og_saved_vm2)
    MODULE_CONFIG(og_saved_vm_ok)
MODULE_CONFIGS_END()

MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_MOVIE_CONTROL_PARAM)
    MODULE_PROPHANDLER(PROP_MOVIE_CROP_MODE)
    MODULE_PROPHANDLER(PROP_HDMI_CHANGE)
    MODULE_PROPHANDLER(PROP_LV_ACTION)
    MODULE_PROPHANDLER(PROP_TEMP_DRAM)
    MODULE_PROPHANDLER(PROP_TEMP_MIMI_L)
    MODULE_PROPHANDLER(PROP_TEMP_MIMI_R)
    MODULE_PROPHANDLER(PROP_TEMP_A)
    MODULE_PROPHANDLER(PROP_TEMP_TFT)
    MODULE_PROPHANDLER(PROP_TEMP_SH)
    MODULE_PROPHANDLER(PROP_TEMP_STATUS)
    MODULE_PROPHANDLER(PROP_BATTERY_REPORT)
MODULE_PROPHANDLERS_END()
