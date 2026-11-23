/** \file
 * R6.190: ML's LiveView image is Canon's FaceYuv buffer (CONFIG_FACEYUV_LV).
 *
 * The R6 has no known LV display buffer that ML can read
 * (YUV422_LV_BUFFER_DISPLAY_ADDR is 0). Canon's face detection works on its
 * own copy of the LV image, "FaceYuv" (LiveViewComp::LvFaceYuvController.c):
 * 8-bit UYVY with signed chroma (0 = neutral, as on DIGIC 4/5), 4 buffers
 * on IBus channel 1, which the CPU reads through the uncached alias of a
 * static memif window. Its size follows the LV mode: 640x426 (3:2) in
 * photo mode, 640x360 (16:9) in movie mode, also while recording (seen on
 * camera).
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
 *    FY_REC_STRIDE * k: +4 FrameNum, +0x10 the 64-bit buffer address,
 *    + FY_REC_W / FY_REC_H the frame's size
 *  - the next-buffer getter 0xE0809390 hands out buffer [ctx + FY_GET_IDX]
 *    and then increments that byte mod 4
 * So: the record with the newest ring FrameNum (or the newest older one)
 * gives the buffer, if it is one of the 4 and of the current size. Without
 * a record, take the buffer handed out before the most recent one (that
 * one may still be written).
 *
 * After a photo <-> movie change, the 4 buffers and FY_SIZE change first;
 * for about 2 s the records (and FY_INFO) still describe frames of the old
 * size, in the old buffers (seen on camera). So the size is checked at
 * every source, and such a frame is not used.
 */

#include "dryos.h"
#include "property.h"
#include "propvalues.h"
#include "vram.h"

#define FY_STATE      0x1ED68   /* s8 [+0] ring index, [+0xC] ctx */
#define FY_CTX_SIZE   0xB060
#define FY_RING       0x164614  /* 3 x {flag, FrameNum} */
#define FY_GET_IDX    0xB029
#define FY_REC        0x40
#define FY_REC_STRIDE 0xA0
#define FY_REC_W      0x1C      /* u32 width of the record's frame */
#define FY_REC_H      0x20      /* u32 height */
#define FY_MEMIF_WIN  0x1355A4  /* RAM copy of the 12 static memif windows (0xE089F230); [11] = copied */
#define FY_CH_LIMIT   0x106484  /* RAM end of DRAM channel 1 (MemifApiInit 0xE01053BC) */
#define FY_STALE_MS   1000

/* FaceYuv size of the current LV mode, u16 width, u16 height: LVIC copies
 * it here (0xE05D5854) from the FaceYuv size table FY_STATE + 0x20, picked
 * by 0xE08094C0 */
#define FY_SIZE       0x11FC8

/* copy of the newest LV record from its + 0x10 (0xE08D8B64):
 * + 0xC width, + 0x10 height */
#define FY_INFO       0x114520

/* [[DISP_DEV_PTR] + 0x18] = display outputs (0xE01694C8):
 * bit 0 LCD panel, bit 4 EVF, 0x1100 HDMI / line out */
#define DISP_DEV_PTR  0xEA40
#define DISP_LCD      0x01
#define DISP_EVF      0x10

/* Canon's live image info per output: DISPIMAGEINFO (builder 0xE02995A8),
 * copied by dispVramCtrl_SetOnlyParamIMGRepVram 0xE061DF20 to DVC + 0x1C8
 * (+ 0x260 when double buffered) for the LCD, + 0x390 (+ 0x428) for the
 * EVF. Live slot: +0 == 1, +4 == 0 (LCD) / 1 (EVF). Seen on camera:
 * +0x14 / +0x18 buffer size, +0x24 / +0x28 the image's position on the
 * panel, +0x2C / +0x30 its size. */
#define DISP_VRAM_CTRL_PTR 0xEA60
#define DISP_IMG_X    0x24
#define DISP_IMG_Y    0x28
#define DISP_IMG_W    0x2C
#define DISP_IMG_H    0x30

/* The layouts we map: Canon's image on the panel (as in the BlackBelt
 * table, lookup 0xE080BEC4, and seen on camera), and where ML's 3:2 LV
 * area (720x480, full width) starts, with the 16:9 bars inside it that the
 * movie image leaves (ML's os.off_169; the FaceYuv image is only the part
 * between them).
 * LCD: 900x600 panel = ML's 720x480 (0.8 ML rows per panel row).
 * EVF: 1280x960 panel, ML's 720x480 on its middle 1280x854 band (rows
 * 53..906, 480 / 854 ML rows per panel row), so the 3:2 area at the
 * panel's top starts at ML row -30.
 * Movie: LCD image rows 47..552 -> ML 37.6..442.4, i.e. 38..441 (bars of
 * 38 rows); EVF rows 67..786 -> ML 7.9..412.6, i.e. 38 rows below -30. */
struct fy_layout
{
    uint16_t fy_w, fy_h;            /* FaceYuv size (FY_SIZE) */
    uint16_t out;                   /* DISP_LCD or DISP_EVF */
    uint16_t img_w, img_h, img_y;   /* Canon's image on the panel, at x = 0 */
    int16_t  y0;                    /* ML row of the 3:2 area's top */
    int16_t  off_169;               /* 16:9 bars, ML rows each */
};

static const struct fy_layout fy_layouts[] = {
    /* photo mode (3:2) */
    { 640, 426, DISP_LCD,  900, 600,  0,   0,  0 },
    { 640, 426, DISP_EVF, 1280, 854,  0, -30,  0 },
    /* movie mode (16:9) */
    { 640, 360, DISP_LCD,  900, 506, 47,   0, 38 },
    { 640, 360, DISP_EVF, 1280, 720, 67, -30, 38 },
};

/* the layout vram.c is set up for (faceyuv_lv_geometry) */
static const struct fy_layout * volatile fy_layout = &fy_layouts[0];

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

/* CPU address (uncached alias) of the newest complete w x h FaceYuv frame,
 * or 0; *frame = its FrameNum */
static uint32_t faceyuv_locate(uint32_t * frame, uint32_t w, uint32_t h)
{
    const uint32_t bytes = w * 2 * h;
    if (!uncached_ok()) return 0;

    uint32_t ctx = RD(FY_STATE + 0xC);
    if (!ram_ok(ctx, FY_CTX_SIZE)) return 0;
    if (RD(FY_SIZE) != ((h << 16) | w)) return 0;

    int ring_i = (int8_t) rd8(FY_STATE);
    if (ring_i < 0 || ring_i > 2) return 0;
    *frame = RD(FY_RING + 8 * ((ring_i + 2) % 3) + 4);

    uint32_t blo[4], bhi[4];
    for (int k = 0; k < 4; k++)
        if (!rd64(ctx + 0x20 + 8 * k, &blo[k], &bhi[k])) return 0;

    /* the newest record not newer than the ring's frame; records of
     * another size are from before a mode change */
    int pick = -1, best_d = 16;
    for (int k = 0; k < 4; k++)
    {
        uint32_t rec = ctx + FY_REC + FY_REC_STRIDE * k, lo, hi;
        uint32_t f = RD(rec + 4);
        if (RD(rec + FY_REC_W) != w || RD(rec + FY_REC_H) != h) continue;
        if (!rd64(rec + 0x10, &lo, &hi)) continue;
        int m = -1;
        for (int j = 0; j < 4; j++)
            if (lo == blo[j] && hi == bhi[j]) m = j;
        int d = (int) (*frame - f);
        if (m >= 0 && d >= 0 && d < best_d) { best_d = d; pick = m; }
    }
    if (pick < 0)
    {
        /* the newest frame must have the current size, too */
        if (RD(FY_INFO + 0xC) != w || RD(FY_INFO + 0x10) != h) return 0;
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
        uint32_t m = RD(FY_MEMIF_WIN + 4 * i);
        if ((m & 7) == hi && (m & 0xF0000000) == (lo & 0xF0000000) && !(m & 0x0FFFFFF8))
            win = i;
    }
    if (win < 4 || win > 7) return 0;
    if ((lo & 0x0FFFFFFF) + bytes > 0x10000000) return 0;
    uint32_t lim = RD(FY_CH_LIMIT);
    if (!lim || lo + bytes < lo || lo + bytes > lim) return 0;

    uint32_t cpu = ((uint32_t) win << 28) | (lo & 0x0FFFFFFF);
    return ram_ok(cpu, bytes) ? cpu : 0;
}

/* the live slot of one output: 1 and its image size and position, or 0 */
static int disp_image(int evf, uint32_t * w, uint32_t * h, uint32_t * x, uint32_t * y)
{
    static const uint16_t slots[2][2] = { { 0x1C8, 0x260 }, { 0x390, 0x428 } };
    uint32_t dvc = RD(DISP_VRAM_CTRL_PTR);
    for (int s = 0; s < 2; s++)
    {
        uint32_t b = dvc + slots[evf][s];
        if (!dvc || !ram_ok(b, DISP_IMG_H + 4)) return 0;
        if (RD(b) != 1 || RD(b + 4) != (uint32_t) evf) continue;
        *w = RD(b + DISP_IMG_W); *h = RD(b + DISP_IMG_H);
        *x = RD(b + DISP_IMG_X); *y = RD(b + DISP_IMG_Y);
        return 1;
    }
    return 0;
}

/* the layout for this output and FaceYuv size, or NULL (HDMI, an image
 * not in the table, no live slot) */
static const struct fy_layout * fy_find_layout(uint32_t out, uint32_t size)
{
    for (int i = 0; i < COUNT(fy_layouts); i++)
    {
        const struct fy_layout * l = &fy_layouts[i];
        if (l->out != out || size != (((uint32_t) l->fy_h << 16) | l->fy_w)) continue;
        uint32_t w, h, x, y;
        if (!disp_image(out == DISP_EVF, &w, &h, &x, &y)) return NULL;
        return (w == l->img_w && h == l->img_h && x == 0 && y == l->img_y) ? l : NULL;
    }
    return NULL;
}

void faceyuv_lv_geometry(int * width, int * height, int * y0, int * off_169)
{
    const struct fy_layout * l = fy_layout;
    *width = l->fy_w;
    *height = l->fy_h;
    *y0 = l->y0;
    *off_169 = l->off_169;
}

void * faceyuv_lv_buffer(void)
{
    static uint32_t last_frame = 0;
    static int t_frame = 0, frame_seen = 0;

    if (!lv)
    {
        frame_seen = 0;
        return NULL;
    }

    /* LCD or EVF alone; not HDMI (other geometry) */
    uint32_t dev = RD(DISP_DEV_PTR);
    if (!ram_ok(dev, 0x1C)) return NULL;
    uint32_t outputs = RD(dev + 0x18);
    if (outputs != DISP_LCD && outputs != DISP_EVF) return NULL;

    /* photo (3:2) or movie (16:9) image, on a screen layout we know */
    const struct fy_layout * l = fy_find_layout(outputs, RD(FY_SIZE));
    if (!l) return NULL;
    if (l != fy_layout)
    {
        /* vram_lv and os follow (src/vram.c, on the next
         * get_yuv422_vram); this frame is not used */
        fy_layout = l;
        vram_params_set_dirty();
        frame_seen = 0;
        return NULL;
    }

    uint32_t frame = 0;
    uint32_t cpu = faceyuv_locate(&frame, l->fy_w, l->fy_h);
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
