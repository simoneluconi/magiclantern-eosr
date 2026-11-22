/** \file
 * R6.190: ML's LiveView image is Canon's FaceYuv buffer (CONFIG_FACEYUV_LV).
 *
 * The R6 has no known LV display buffer that ML can read
 * (YUV422_LV_BUFFER_DISPLAY_ADDR is 0). Canon's face detection works on its
 * own copy of the LV image, "FaceYuv" (LiveViewComp::LvFaceYuvController.c):
 * FACEYUV_WIDTH x FACEYUV_HEIGHT (640x426), 8-bit UYVY with signed chroma
 * (0 = neutral, as on DIGIC 4/5), 4 buffers on IBus channel 1, which the
 * CPU reads through the uncached alias of a static memif window.
 * Format, location and the buffer choice below are from andrewpoison's
 * R6.190 port (commits 9a507a9b9 and 3e3b12452, r6_faceyuv.c), checked on
 * his camera; the EVF layout from cd5757575 (r6_gdraw.c).
 *
 * Read only: plain loads from Canon's RAM, no Canon call, nothing written.
 * Page 0 is unmapped on R6, so every pointer taken from Canon's structures
 * is range-checked before it is dereferenced.
 *
 * Buffer choice (ROM 1.9.0):
 *  - ctx = MEM(FY_STATE + 0xC) (FY_CTX_SIZE bytes); the 4 buffers are
 *    64-bit IBus addresses at ctx + 0x20 + 8 * k (copied at
 *    0xE0809376..0xE0809386)
 *  - the per-frame step 0xE0809212 stores {flag, FrameNum} into the
 *    3-entry ring FY_RING at index s8 MEM(FY_STATE), then increments it
 *    mod 3, so the newest entry is (index + 2) % 3
 *  - a consumer request 0xE0809550 fills record ctx + FY_REC +
 *    FY_REC_STRIDE * k: +4 FrameNum, +0x10 the 64-bit buffer address
 *  - the next-buffer getter 0xE0809390 hands out buffer [ctx + FY_GET_IDX]
 *    and then increments that byte mod 4
 * So: the record with the newest ring FrameNum (or the newest older one)
 * gives the buffer, if it is one of the 4. Without a record, take the
 * buffer handed out before the most recent one (that one may still be
 * written).
 */

#include "dryos.h"
#include "property.h"
#include "propvalues.h"
#include "vram.h"

#define FY_STATE      0x1ED68   /* s8 [+0] ring index, [+0xC] ctx */
#define FY_CTX_SIZE   0xB060
#define FY_RING       0x164614  /* 3 x {flag, FrameNum} */
#define FY_SIZE       0x11FC8   /* u16 width, u16 height of the FaceYuv image */
#define FY_GET_IDX    0xB029
#define FY_REC        0x40
#define FY_REC_STRIDE 0xA0
#define FY_MEMIF_WIN  0x1355A4  /* RAM copy of the 12 static memif windows (0xE089F230); [11] = copied */
#define FY_CH_LIMIT   0x106484  /* RAM end of DRAM channel 1 (MemifApiInit 0xE01053BC) */
#define FY_BYTES      (FACEYUV_WIDTH * 2 * FACEYUV_HEIGHT)
#define FY_STALE_MS   1000

/* [[DISP_DEV_PTR] + 0x18] = display outputs (0xE01694C8):
 * bit 0 LCD panel, bit 4 EVF, 0x1100 HDMI / line out */
#define DISP_DEV_PTR  0xEA40
#define DISP_LCD      0x01
#define DISP_EVF      0x10

/* Canon's live EVF image info: DISPIMAGEINFO (builder 0xE02995A8) copied
 * for the EVF to DVC + 0x390 (dispVramCtrl_SetOnlyParamIMGRepVram
 * 0xE061DF20), + 0x428 when double buffered. Live slot: +0 == 1 and
 * +4 == 1. +0x14 / +0x18 buffer size, +0x24 / +0x28 position in the panel. */
#define DISP_VRAM_CTRL_PTR 0xEA60

/* In the EVF, Canon shows the 1280x854 image at the top of the 1280x960
 * panel, i.e. at ML rows -30..449 (cd5757575, BlackBelt lookup 0xE080BEC4) */
#define FACEYUV_EVF_Y0 (-30)

#define RD(a) (*(volatile uint32_t *)(a))

/* windows 4..7 of the memif table are the uncached aliases of 0..3 */
static int uncached_ok(void)
{
    if (!RD(FY_MEMIF_WIN + 0x2C)) return 0;
    for (int i = 0; i < 4; i++)
        if (RD(FY_MEMIF_WIN + 4 * (i + 4)) != RD(FY_MEMIF_WIN + 4 * i)) return 0;
    return 1;
}

/* 1 = cached RAM, 2 = uncached alias, 0 = neither */
static int region(uint32_t a)
{
    if (a >= 0x1000 && a < 0x40000000) return 1;
    if (a >= 0x40001000 && a < 0x80000000) return 2;
    return 0;
}

/* [a, a + n) aligned and inside one of the two regions */
static int ram_ok(uint32_t a, uint32_t n)
{
    if ((a & 3) || n < 4) return 0;
    int r = region(a);
    return r && a + n - 1 > a && region(a + n - 1) == r;
}

/* byte at a, via the aligned word that holds it */
static uint32_t rd8(uint32_t a)
{
    return (RD(a & ~3u) >> (8 * (a & 3))) & 0xFF;
}

/* 64-bit value, read until two reads match (at most 4 tries) */
static int rd64(uint32_t a, uint32_t * lo, uint32_t * hi)
{
    if (!ram_ok(a, 8)) return 0;
    for (int i = 0; i < 4; i++)
    {
        uint32_t l1 = RD(a), h1 = RD(a + 4);
        uint32_t l2 = RD(a), h2 = RD(a + 4);
        if (l1 == l2 && h1 == h2) { *lo = l1; *hi = h1; return 1; }
    }
    return 0;
}

/* CPU address (uncached alias) of the newest complete FaceYuv frame, or 0;
 * *frame = its FrameNum */
static uint32_t faceyuv_locate(uint32_t * frame)
{
    if (!uncached_ok()) return 0;

    uint32_t ctx = RD(FY_STATE + 0xC);
    if (!ram_ok(ctx, FY_CTX_SIZE)) return 0;
    if (RD(FY_SIZE) != (((uint32_t) FACEYUV_HEIGHT << 16) | FACEYUV_WIDTH)) return 0;

    int ring_i = (int8_t) rd8(FY_STATE);
    if (ring_i < 0 || ring_i > 2) return 0;
    *frame = RD(FY_RING + 8 * ((ring_i + 2) % 3) + 4);

    uint32_t blo[4], bhi[4];
    for (int k = 0; k < 4; k++)
        if (!rd64(ctx + 0x20 + 8 * k, &blo[k], &bhi[k])) return 0;

    /* the newest record not newer than the ring's frame */
    int pick = -1, best_d = 16;
    for (int k = 0; k < 4; k++)
    {
        uint32_t rec = ctx + FY_REC + FY_REC_STRIDE * k, lo, hi;
        uint32_t f = RD(rec + 4);
        if (!rd64(rec + 0x10, &lo, &hi)) continue;
        int m = -1;
        for (int j = 0; j < 4; j++)
            if (lo == blo[j] && hi == bhi[j]) m = j;
        int d = (int) (*frame - f);
        if (m >= 0 && d >= 0 && d < best_d) { best_d = d; pick = m; }
    }
    if (pick < 0)
    {
        uint32_t g = rd8(ctx + FY_GET_IDX);
        if (g > 3) return 0;
        pick = (g + 2) & 3;
    }
    uint32_t lo = blo[pick], hi = bhi[pick];

    /* IBus channel 1, in a static window 4..7 (same channel and 256 MiB
     * slice, Canon's rule at 0xE089F2B8..0xE089F2D4), the whole buffer
     * inside the window and inside channel 1's RAM */
    if (hi != 1 || (lo & 3)) return 0;
    int win = -1;
    for (int i = 4; i < 12 && win < 0; i++)
    {
        uint32_t w = RD(FY_MEMIF_WIN + 4 * i);
        if ((w & 7) == hi && (w & 0xF0000000) == (lo & 0xF0000000) && !(w & 0x0FFFFFF8))
            win = i;
    }
    if (win < 4 || win > 7) return 0;
    if ((lo & 0x0FFFFFFF) + FY_BYTES > 0x10000000) return 0;
    uint32_t lim = RD(FY_CH_LIMIT);
    if (!lim || lo + FY_BYTES < lo || lo + FY_BYTES > lim) return 0;

    uint32_t cpu = ((uint32_t) win << 28) | (lo & 0x0FFFFFFF);
    return ram_ok(cpu, FY_BYTES) ? cpu : 0;
}

/* only the EVF layout we map: the 1280x854 stills image at the panel's top */
static int evf_layout_ok(void)
{
    static const uint16_t slots[2] = { 0x390, 0x428 };
    uint32_t dvc = RD(DISP_VRAM_CTRL_PTR);
    for (int s = 0; s < 2; s++)
    {
        uint32_t b = dvc + slots[s];
        if (!dvc || !ram_ok(b, 0x2C)) return 0;
        if (RD(b) != 1 || RD(b + 4) != 1) continue;
        return RD(b + 0x14) == 1280 && RD(b + 0x18) == 854 && RD(b + 0x24) == 0 && RD(b + 0x28) == 0;
    }
    return 0;
}

static volatile int on_evf = 0;

int faceyuv_lv_y0(void)
{
    return on_evf ? FACEYUV_EVF_Y0 : 0;
}

void * faceyuv_lv_buffer(void)
{
    static uint32_t last_frame = 0;
    static int t_frame = 0, frame_seen = 0;

    /* Canon's LV runs, photo mode: the movie LV layout is not checked */
    if (!lv || is_movie_mode() || lv_movie_select == LVMS_ENABLE_MOVIE)
    {
        frame_seen = 0;
        return NULL;
    }

    /* LCD or EVF alone; not HDMI (other geometry) */
    uint32_t dev = RD(DISP_DEV_PTR);
    if (!ram_ok(dev, 0x1C)) return NULL;
    uint32_t outputs = RD(dev + 0x18);
    if (outputs != DISP_LCD && outputs != DISP_EVF) return NULL;
    if (outputs == DISP_EVF && !evf_layout_ok()) return NULL;
    int evf = (outputs == DISP_EVF);
    if (evf != on_evf)
    {
        /* os.y0 and bm2lv follow the screen (src/vram.c) */
        on_evf = evf;
        vram_params_set_dirty();
    }

    uint32_t frame = 0;
    uint32_t cpu = faceyuv_locate(&frame);
    if (!cpu) return NULL;

    /* a ring that stopped moving (LV paused) is not a live image */
    int now = get_ms_clock();
    if (!frame_seen || frame != last_frame)
    {
        last_frame = frame;
        t_frame = now;
        frame_seen = 1;
    }
    else if (now - t_frame > FY_STALE_MS || now < t_frame)
    {
        return NULL;
    }

    return (void *) cpu;
}
