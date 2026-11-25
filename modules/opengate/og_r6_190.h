/** \file
 * Open gate 4K: every Canon address, size and stock value for the R6 1.9.0.
 *
 * Sources are the research notes in ML-R6/research/opengate (private): SENSOR.md,
 * PIPELINE.md, AUDIT.md, FRESH-REVIEW.md, FPS25.md, ENCODER-2736.md, QUALITY.md,
 * T4-INDEPENDENT.md, and the camera runs in probes/RESULTS.md (runs 1-49, proven
 * with the test module ogprobe on branch r6.190-ogprobe2). Every value here was read
 * from the ROM dump or on camera; the module checks each stock value before it
 * writes anything.
 */

#ifndef _og_r6_190_h_
#define _og_r6_190_h_

/* ---- Canon RAM state (read only) ---- */
#define OG_IMGCTRL      0x17658     /* +8 sensor slot, +0x10 VD (SENSOR.md) */
#define OG_LV_DIVIDER   0x11E28     /* LV frame divider */
#define OG_LVCAP        0x1200C     /* +0x3C capture-mode -> sensor slot map pointer */
#define OG_Y2V          0x12AA8     /* +0x64 path id: 3 = 4K movie, 0x35 = photo LV */
#define OG_FRAMEDATA    0x130A68    /* +0x24 LvCapMode, +0x28 programmed slot (FPS25 1.1) */
#define OG_TIMING_INFO  0x11EA8     /* +0x20 timing table pointer, +0x24 fps index (= column) */
#define OG_BLOB_PTRS    0x375B4     /* +0x18 / +0x1C live CMOS / DIGIC blobs */
#define OG_MEMNAV       0xA000      /* MemoryNavigator object pointer, +8 active sub-map */

#define OG_PATH_MOVIE   3
#define OG_PATH_PHOTO   0x35
#define OG_CAPMODE_4K30 0x1B        /* 4K UHD <= 30p, no crop */
#define OG_CAPMODE_4KCROP 0x33      /* 4K crop <= 30p */
#define OG_SLOT_4K30    10          /* stock sensor slot of 0x1B */
#define OG_SLOT_FULL    9           /* 4K 50p slot: full-height line time, used by open gate */

/* ---- sensor slot 9 at full height (SENSOR.md T2/T5; RESULTS write test 2) ----
 * Live DRAM blobs, edited in place; stock / new pairs. Entries with stock == new are
 * address checks of the register list. */
#define OG_BLOB_CMOS    0x42B8116C
#define OG_BLOB_DIGIC   0x42B8AA48
struct og_cell { uint32_t addr; uint8_t size; uint32_t stock, val; };
static const struct og_cell og_sensor_cells[] =
{
    { 0x42B83B44, 4, 0x157540FD, 0x157B403D },     /* CMOS ModeInit reg 0x055 (rows) */
    { 0x42B83B98, 4, 0x1FCBB418, 0x1FCEB418 },     /* CMOS ModeInit reg 0x07F (output rows - 4) */
    { 0x42B8905C, 4, 0x157540FD, 0x157B403D },     /* CMOS SR list reg 0x055 */
    { 0x42B8FD90, 4, 0xD2413064, 0xD2413064 },     /* check: D2413064 */
    { 0x42B8FD94, 4, 0x00000341, 0x00000401 },     /* readout HD - 1 (942 + 84 DAF - 1) */
    { 0x42B8FDB8, 4, 0xD24124C0, 0xD24124C0 },
    { 0x42B8FDBC, 4, 0x0BB7160F, 0x0EB7160F },     /* DIGIC read window 3000 -> 3768 rows */
    { 0x42B8FDC8, 4, 0xD24124D0, 0xD24124D0 },
    { 0x42B8FDCC, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8FDD0, 4, 0xD241252C, 0xD241252C },
    { 0x42B8FDD4, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8FDE0, 4, 0xD241253C, 0xD241253C },
    { 0x42B8FDE4, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8FDE8, 4, 0xD2412594, 0xD2412594 },
    { 0x42B8FDEC, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8FDF8, 4, 0xD24125A4, 0xD24125A4 },
    { 0x42B8FDFC, 4, 0x0BB7160F, 0x0EB7160F },
    /* HEAD output of slot 9 (RESULTS test 3a) */
    { 0x42B8C470, 4, 0xD2412B04, 0xD2412B04 },
    { 0x42B8C474, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8C480, 4, 0xD2412B0C, 0xD2412B0C },
    { 0x42B8C484, 4, 0x0BB7160F, 0x0EB7160F },
    { 0x42B8C488, 4, 0xD2412B10, 0xD2412B10 },
    { 0x42B8C48C, 4, 0x0BB70000, 0x0EB70000 },
    /* line time 524 -> 576 ticks (research HD576.md 8; sensor mode 3's full-height 1x1 line
     * time): CMOS ModeInit 0x041 / 0x043 and DIGIC D2408198 / D240819C = HD - 1 / HD. Canon
     * rebuilds the sensor window words 0x027-0x02E from the timing cells by itself */
    { 0x42B83B24, 4, 0x1040020B, 0x1040023F },     /* CMOS ModeInit reg 0x041 = HD - 1 */
    { 0x42B83B28, 4, 0x10C0020B, 0x10C0023F },     /* CMOS ModeInit reg 0x043 = HD - 1 */
    { 0x42B8BA00, 4, 0xD2408198, 0xD2408198 },     /* check: D2408198 */
    { 0x42B8BA04, 4, 0x0000020B, 0x0000023F },     /* D2408198 = HD - 1 */
    { 0x42B8BA08, 4, 0xD240819C, 0xD240819C },     /* check: D240819C */
    { 0x42B8BA0C, 4, 0x0000020C, 0x00000240 },     /* D240819C = HD */
};
/* the four line-time words above, put back by the property handler if it cannot take the
 * module lock at a change to a setting the open gate does not support (og_lost): a slot-9
 * movie mode (4K 50/59.94p) must never start with the HD-576 words on Canon's HD-524 cells */
static const uint32_t og_hd_words[4] = { 0x42B83B24, 0x42B83B28, 0x42B8BA04, 0x42B8BA0C };
/* SSG D2408280/90/98 are derived by Canon from ROD: never written (RESULTS run 2). */

/* ---- timing table row 15, the column of the movie fps (FPS25.md 1.2) ----
 * u16 cell = TIMING + off + 15*0x20 + 2*col; cartridge u32 = TIMING + 0x1180 + 15*0x40 + 4*col.
 * Column = Canon's fps index: 2 = 29.97p, 3 = 25p, 4 = 23.98p. Line time HD 576 (21 us),
 * full-height ROD 1027 (research HD576.md 3, Canon's rules): FL = floor(period / HD), RPS =
 * floor((period - 1 ms) / HD); 25p takes FL 1874, not 1875 (1875 x 576 is the exact period,
 * Canon always leaves a positive trim; VD reads 1875). The BUF_A FIFO fills 9 % slower than
 * at slot 9's own HD 524, which left too little margin at 29.97p (camera session
 * 2026-11-25-hd576: 3:2 29.97p 3 x 5 min, 4:3 29.97p 3 min, 3:2 23.98p, clean). */
#define OG_TIMING_TABLE 0x42BAA11C
#define OG_TCELL(off, col)  (OG_TIMING_TABLE + (off) + 15 * 0x20 + 2 * (col))
#define OG_TCART(col)       (OG_TIMING_TABLE + 0x1180 + 15 * 0x40 + 4 * (col))
/* fps100 = Canon's exact rate x 100 (PROP_MOVIE_CONTROL_PARAM word 1). 24.00p is its own
 * fps index 9 (column 9, zero in the stock table, 60.00 Hz base sync; fps24/FPS24.md), not
 * column 4: not supported (2400 is no rate here, and the live check refuses column 9) */
struct og_rate { uint8_t col; const char * name; uint16_t fl_stock, fl, rps_stock, rps, fps100; };
static const struct og_rate og_rates[] =
{
    { 2, "29.97p", 790, 1564, 766, 1517, 2997 },
    { 3, "25p",    947, 1874, 923, 1828, 2500 },
    { 4, "23.98p", 987, 1955, 964, 1908, 2398 },
};
/* SetP (+0x280: the line of the next-frame setup interrupt, evfSetParamInterrupt) and the
 * cartridge (ESync output 2, us) follow Canon's rule for the readout end: SetP = ROD -
 * round(109,000 / HD), 4.04 ms before the readout end; cartridge = ceil(SetP * HD / 27) +
 * 1000 us. The 50p values 627 / 0x3371 (readout end 835) started that work inside our longer
 * readout: every BUF_A error logged then fell ~4.9 ms after SetP, at VD + 17.07 ms (research
 * BUFA-RINGS.md; fixed with 819 / 0x41FF at HD 524, session 2026-11-25-setp) */
#define OG_T_SETP_STOCK  655        /* +0x280 */
#define OG_T_SETP        838
#define OG_T_ROD_STOCK   751        /* +0x500 */
#define OG_T_ROD         1027
#define OG_T_HD_STOCK    1140       /* +0xA00, HD ticks */
#define OG_T_HD          576
#define OG_T_HDUS_STOCK  42         /* +0xC80, HD us = floor(HD / 27) */
#define OG_T_HDUS        21
#define OG_T_FLAG        1          /* +0xF00, check only */
#define OG_T_CART_STOCK  0x6FF0
#define OG_T_CART        0x49BE     /* ceil(838 * 576 / 27) + 1000 us */

/* ---- raw ring: MemoryNavigator LV raw entry (type 0x12) of both 4K sub-maps ----
 * {region 5, offset 0, slot 0x16D4DE0, count 3} -> 2 slots of 0x1E70000 (RESULTS run 4) */
#define OG_MNAV_SUB_A   0x490
#define OG_MNAV_SUB_B   0xED0
#define OG_MNAV_RAW     (0x10 * 0x12)
#define OG_RAW_SLOT_STOCK 0x16D4DE0
#define OG_RAW_N_STOCK    3
#define OG_RAW_SLOT       0x1E70000
#define OG_RAW_N          2

/* ---- capture-mode map: LvCapMode 0x1B -> slot 9 (FPS25.md 1.1) ---- */
#define OG_MAP_ROM      0xE0FEEE60  /* copy start (keeps the two u16 tables in front) */
#define OG_MAP_TBL      0xE0FEEF00
#define OG_MAP_LEN      0x1B4
#define OG_LIT_MAP1     0xE03247EC  /* SensorDrive: the slot that is programmed */
#define OG_LIT_MAP2     0xE0325D0C  /* sensor-mode change check */

/* ---- SAP input frame size per sensor mode (PIPELINE.md Round 9) ---- */
#define OG_SAP_TBL      0xE1EE18AC
#define OG_SAP_LEN      0x140
#define OG_LIT_SAP      0xE037EA20

/* ---- MvRawPro readout tables, record #0 at 3660 rows from y 102 (PIPELINE.md R4.2) ---- */
#define OG_MVRAW_LO     0xE1F6521C
#define OG_MVRAW_HI     0xE1F66A60
#define OG_MVRAW_SUB    0xE1F65738
#define OG_REC0         0xE1F65784
#define OG_LIT_KEYS     0xE07F9EA8
#define OG_LIT_SUB      0xE07F9EAC
struct og_edit { uint32_t addr; uint32_t stock, val; };
static const struct og_edit og_rec_edits[] =
{
    { OG_REC0 + 0x1C, 3000, 3768 }, { OG_REC0 + 0x2C, 96, 102 }, { OG_REC0 + 0x34, 2904, 3660 },
    { OG_REC0 + 0x4C, 3000, 3768 }, { OG_REC0 + 0x58, 96, 102 }, { OG_REC0 + 0x60, 2904, 3660 },
    { OG_REC0 + 0x74, 2904, 3660 }, { OG_REC0 + 0x94, 2903, 3659 }, { OG_REC0 + 0xA4, 2904, 3660 },
    { OG_REC0 + 0xB4, 2904, 3660 }, { OG_REC0 + 0xDC, 2904, 3660 }, { OG_REC0 + 0xEC, 2904, 3660 },
    { OG_REC0 + 0x3C, 1548, 1932 }, { OG_REC0 + 0x7C, 1452, 1830 },
    { 0xE1F663B8 + 0x18, 2886, 3654 }, { 0xE1F663B8 + 0x40, 720, 912 },
    { 0xE1F66698 + 0x20, 2903, 3659 }, { 0xE1F66698 + 0x30, 2904, 3660 },
    { 0xE1F66698 + 0x48, 967, 1219 }, { 0xE1F66698 + 0x58, 966, 1218 },
};

/* ---- OB integration sizes, 4K FIT entry 5496x2904 -> 5496x3660 (AUDIT R6.2) ---- */
#define OG_OBI_LO       0xE1F374DC
#define OG_OBI_HI       0xE1F375A8
#define OG_OBI_H_4K     0xE1F374F8
#define OG_LIT_OBI      0xE039AB34

/* ---- LV develop record of path 3, behind a .data pointer (FRESH-REVIEW.md) ---- */
#define OG_DEV_PTR      0x3F0A0
#define OG_DEV_REC      0xE1F7F480
#define OG_DEV_LEN      0x9B0
#define OG_DEV_PIPE1    0x384       /* vertical fields of pipe 1 */
#define OG_DEV_SCALE1   0x60        /* scale block of pipe 1 */
#define OG_DEV_DMA1     0xB4        /* pyramid EDMAC descriptors of pipe 1 */

/* ---- MvPostDevelop of path 3 and its display crop table (ENCODER-2736.md 6) ---- */
#define OG_POST_PTR     0x2DB5C
#define OG_POST_REC     0xE1CA7D58
#define OG_POST_LEN     0xF0
#define OG_CROP_TBL     0xE1CA7B00
#define OG_CROP_LEN     0x50

/* ---- SAP stage 8 (HIVSHD) per-row tables (QUALITY.md T6 follow-up) ---- */
#define OG_ST8_CTX      0x38C78     /* +0xC V offset knots, +0x14 VIR knots */
#define OG_ST8_VIR_BASE 0x1F3A8     /* *(.)+0x148+4i, i < 26 */
#define OG_ST8_VOFF_BASE 0x1F064    /* *(.)+0x780+4k, k < 4 */
#define OG_ST8_VIR_N    26
#define OG_ST8_VOFF_N   4
#define OG_ST8_KNOTS_STOCK 96
#define OG_ST8_KNOTS    120

/* ---- ROM words (code or data), each with its stock value ---- */
struct og_word { uint32_t addr, stock, val; uint8_t insn; const char * what; };

/* SAP stage 2/6/8 windows to row 3767 (AUDIT P2-P4; RESULTS run 15) */
static const struct og_word og_sapwin_words[] =
{
    { 0xE0C7780C, 0x0BB70087, 0x0EB70087, 0, "OG SAP stage 2 window" },
    { 0xE0C77DD0, 0x0BB70060, 0x0EB70060, 0, "OG SAP stage 6 window" },
    { 0xE0C781FC, 0x308FF640, 0x608FF640, 1, "OG SAP stage 8 window" },
};

/* stage-8 SetPath loads 120 knots for modes 9/16/17, mode 10 keeps 96 (T4-INDEPENDENT 12) */
static const struct og_word og_knot_words[] =
{
    { 0xE0C78160, 0x0B60F04F, 0x0B78F04F, 1, "OG stage 8 knots 120" },
    { 0xE0C78214, 0xE0124658, 0xE0122060, 1, "OG stage 8 mode 10 keeps 96" },
};

/* memmap record 3 frame heights read by ORCA, the LCD and the encoder (ENCODER-2736 6.3b) */
static const struct og_word og_memmap_words[] =
{
    { 0xE0FFA778, 2160, 2736, 0, "OG memmap rec 3 height (4096)" },
    { 0xE0FFA784, 2160, 2736, 0, "OG memmap rec 3 height (3840)" },
};

/* Y2V stage start stores display mode/aspect 0/0: the 3:2 photo window (T4-INDEPENDENT 12) */
static const struct og_word og_disp_words[] =
{
    { 0xE01310C0, 0x981E9017, 0x20009017, 1, "OG LCD 3:2 window (mode)" },
    { 0xE01310C4, 0x981F9015, 0xBF009015, 1, "OG LCD 3:2 window (aspect)" },
};

/* encoder 4096x2736 (ENCODER-2736.md 3 stage B, T4-INDEPENDENT 6-10; RESULTS run 47) */
static const struct og_word og_enc_words[] =
{
    { 0xE03879B8, 0x10240D24, 0x05240D24, 1, "OG enc S1 UHD -> case-8 slot" },
    { 0xE03879BC, 0x08051524, 0x24051524, 1, "OG enc S2 tbb[9] -> default" },
    { 0xE03879C0, 0x5000F44F, 0x5080F44F, 1, "OG enc S3 width 4096" },
    { 0xE03879C4, 0xF44FE001, 0xF6406008, 1, "OG enc S4" },
    { 0xE03879C8, 0x600850F0, 0x601020B0, 1, "OG enc S5 height 2736" },
    { 0xE03879CC, 0xBD706013, 0xBD70BD70, 1, "OG enc S6" },
    { 0xE03B85B4, 0x2704D109, 0x2703BF00, 1, "OG enc S7 size index 3" },
    { 0xE03BB4DC, 0x1A16130E, 0x2116130E, 1, "OG enc S8 ring -> 8K slot" },
    { 0xE03BB520, 0xF44F5600, 0xF44F5680, 1, "OG enc S9 ring width" },
    { 0xE03BB524, 0xE0055787, 0xE005672B, 1, "OG enc S10 ring height" },
    { 0xE03BB538, 0x2A072107, 0x2A072105, 1, "OG enc R1 ring depth 5" },
    { 0xE036A0B4, 0xF8C42107, 0xF8C42105, 1, "OG enc R2 encoder images 5" },
    { 0xE03BB87C, 0x094C0000, 0x08660000, 0, "OG enc M1 image pool" },
    { 0xE03BB450, 0x082AF44F, 0x0860F44F, 1, "OG enc M2 CME" },
    { 0xE03BB440, 0x01B4F44F, 0x01E4F44F, 1, "OG enc M3 reduced image" },
    { 0xE03BB458, 0x3AAAF44F, 0x3AD8F44F, 1, "OG enc M4" },
    { 0xE03761B8, 0x08080608, 0x06080608, 1, "OG enc ORCA resize width 4096" },
    { 0xE03761D8, 0x429A6687, 0x429A66AB, 1, "OG enc ORCA resize height 2736" },
    { 0xE03B713C, 0x900770AA, 0x900770D7, 1, "OG enc B1 codec frame buffer" },
    { 0xE03B7140, 0x7088F04F, 0x70ACF04F, 1, "OG enc B2 codec frame buffer" },
    /* B1 stays in pool 8 on DRAM ch5, as stock, not on ch4 with the raw ring / HEAD write.
     * Room for the larger B1: the four type-8 buffers of pool 8 (literal E03B7320, its only
     * reader ldr.w sb at E03B714A) 0x273F000 -> 0x256F000, still larger than a whole
     * uncompressed picture; VCodecApi only checks them non-zero, 64-aligned and equal. Pool 8
     * 0x0B23C000: 3:2 uses 0x0B09C000, 4:3 exactly 0x0B23C000 (research B1-CH5.md; camera:
     * B1 at 5:07800000) */
    { 0xE03B7320, 0x0273F000, 0x0256F000, 0, "OG enc type-8 buffers (B1 in pool 8)" },
    /* HEVC 10-bit UHD rows (audit/enc2736/lrows_check.py): level 5.1/5.2 -> 6.1 for the
     * 4096x2736 picture size; IPB bitrate x 1.267 (= the area ratio), ALL-I unchanged */
    { 0xE1F1E7CC, 52, 61, 0, "OG enc level 30p ALL-I" },
    { 0xE1F1E7FC, 52, 61, 0, "OG enc level 25p ALL-I" },
    { 0xE1F1E82C, 51, 61, 0, "OG enc level 30p IPB" },
    { 0xE1F1E834, 170000, 215000, 0, "OG enc kbps 30p IPB" },
    { 0xE1F1E85C, 51, 61, 0, "OG enc level 25p IPB" },
    { 0xE1F1E864, 170000, 215000, 0, "OG enc kbps 25p IPB" },
    { 0xE1F1E88C, 51, 61, 0, "OG enc level 30p IPB Light" },
    { 0xE1F1E894, 85000, 108000, 0, "OG enc kbps 30p IPB Light" },
    { 0xE1F1E8BC, 51, 61, 0, "OG enc level 25p IPB Light" },
    { 0xE1F1E8C4, 85000, 108000, 0, "OG enc kbps 25p IPB Light" },
    { 0xE1F1E8EC, 52, 61, 0, "OG enc level 24p ALL-I" },
    { 0xE1F1E91C, 51, 61, 0, "OG enc level 24p IPB" },
    { 0xE1F1E924, 170000, 215000, 0, "OG enc kbps 24p IPB" },
    { 0xE1F1E94C, 51, 61, 0, "OG enc level 24p IPB Light" },
    { 0xE1F1E954, 85000, 108000, 0, "OG enc kbps 24p IPB Light" },
    /* H.264 8-bit UHD rows (Canon Log off; audit/enc2736/h264_check.py), same group: level
     * 5.1 -> 6.1 (5.1 allows 36,864 macroblocks per frame, 4096x2736 has 43,776; 61 is the
     * code of Canon's own H.264 ALL-I rows); IPB bitrate x 1.267 as for HEVC */
    { 0xE1F1D26C, 51, 61, 0, "OG enc H.264 level 30p IPB" },
    { 0xE1F1D274, 120000, 152000, 0, "OG enc H.264 kbps 30p IPB" },
    { 0xE1F1D29C, 51, 61, 0, "OG enc H.264 level 25p IPB" },
    { 0xE1F1D2A4, 120000, 152000, 0, "OG enc H.264 kbps 25p IPB" },
    { 0xE1F1D2CC, 51, 61, 0, "OG enc H.264 level 30p IPB Light" },
    { 0xE1F1D2D4, 60000, 76000, 0, "OG enc H.264 kbps 30p IPB Light" },
    { 0xE1F1D2FC, 51, 61, 0, "OG enc H.264 level 25p IPB Light" },
    { 0xE1F1D304, 60000, 76000, 0, "OG enc H.264 kbps 25p IPB Light" },
    { 0xE1F1D35C, 51, 61, 0, "OG enc H.264 level 24p IPB" },
    { 0xE1F1D364, 120000, 152000, 0, "OG enc H.264 kbps 24p IPB" },
    { 0xE1F1D38C, 51, 61, 0, "OG enc H.264 level 24p IPB Light" },
    { 0xE1F1D394, 60000, 76000, 0, "OG enc H.264 kbps 24p IPB Light" },
};

/* ---- 4:3 mode, 3840x2880 at 25p (ANAMORPHIC-43.md 9, opengate/L3-FREEZE-ENC.md 6.2; camera
 * og43-A 6L0A8867 / 8868). Full sensor height, centre 4864 columns: the develop scales
 * 5472x3648 to 4320x2882 (1.2667 both ways), the store keeps 4096x2880 at x 112 y 1, ORCA crops
 * the centre 3840 at 1:1 ----
 * og_words43: the 4:3 value of words of the tables above (same stock check). A value equal to
 * the stock value is not patched */
struct og_alt { uint32_t addr, val; };
static const struct og_alt og_words43[] =
{
    { 0xE0FFA778, 2880 },           /* memmap rec 3 heights */
    { 0xE0FFA784, 2880 },
    { 0xE01310C4, 0x20029015 },     /* movs r0,#2: aspect 2, the 4:3 photo window */
    /* encoder 3840x2880, ring depth 5. Canon's frame rule: stride align1024(2.5 w) = 10240 B,
     * rows align64(h) = 2880. At 2880 rows the Sitter ME mode (h16 > 2880 -> 2) and the ARM CME
     * mode (w > 4096 -> 2) both stay 1 (L3-FREEZE-ENC.md 5). Each value disassembled over the
     * open-gate words */
    { 0xE03879C0, 0x6070F44F },     /* S3 mov.w r0,#0xF00 */
    { 0xE03879C8, 0x60103040 },     /* S5 movw r0,#0xB40 */
    { 0xE03BB520, 0xF44F6670 },     /* S9 mov.w r6,#0xF00 */
    { 0xE03BB524, 0xE0056734 },     /* S10 mov.w r7,#0xB40 */
    { 0xE03BB87C, 0x08CA0000 },     /* M1 5 x 10240 x 2880 */
    { 0xE03BB450, 0x0866F44F },     /* M2 CME 1024 x 1472 x 10 = 0xE60000 (M3 / M4 cover 2880) */
    { 0xE03761B8, 0x08080608 },     /* ORCA width stock: 3840, a centred 1:1 crop of the store */
    { 0xE03761D8, 0x429A66B4 },     /* ORCA height mov.w r6,#0x5A0 */
    { 0xE03B713C, 0x900770E4 },     /* B1 0x1C80000 >= 2 x 5120 x 2880 */
    { 0xE03B7140, 0x70B4F04F },     /* B2 2 x 4096 x 2880 (pool 6 = B2 / 4, exact) */
};
/* 4:3 only: the encoder size classifier, 3840 x any -> index 3 (bne -> nop, as S7 for 4096) */
static const struct og_word og_enc43_words[] =
{
    { 0xE03B85AC, 0x2703D10D, 0x2703BF00, 1, "OG enc 4:3 size index 3 (3840)" },
};

/* 4:3: geometry of the develop and the store */
#define OG_HIN          5472        /* develop input width */
#define OG_HB0          1376        /* develop input band H of pipe 0 */
#define OG_HOUT_STOCK   4096
#define OG_POST_OFS_TBL 0xE1CA7A40  /* stock MvPostDevelop offset table (+0xB0 / +0xB4) */
#define OG_POST_OFS_FLAGS 0x00010003    /* as Canon's path 4 table E1CA7A4C */

/* 4:3: LV YUV slots, MemoryNavigator type 0 of both 4K sub-maps (region 6). A 4096x2880 store
 * takes ~32.7 MB at the fixed 11,264 B pitch: 4 x 0x1E00000 -> 3 x 0x2200000 (og43-L2c / A) */
#define OG_MNAV_YUV         0x00
#define OG_YUV_SLOT_STOCK   0x1E00000
#define OG_YUV_N_STOCK      4
#define OG_YUV_SLOT_43      0x2200000
#define OG_YUV_N_43         3

/* 4:3: LCD / EVF window records (.data 0x245C8, 0x30 B: panel W,H; VRAM W,H; area W,H; window
 * W,H; x,y in area; x,y in VRAM). ORCA sizes the resizer output from the AREA and the display
 * places it at the VRAM x,y: the 4:3 records 2 / 0xC have the 4:3 window with the 3:2 area
 * (og43-L2b: area W = window W gave the pillarboxed 4:3 picture). The core writes them
 * (lv_display_window_set_frame, CONFIG_LV_DISPLAY_WINDOW); their stock values are checked here */
#define OG_WIN_RECS         0x245C8
#define OG_WIN_LCD43        (OG_WIN_RECS + 0x30 * 0x2)
#define OG_WIN_EVF43        (OG_WIN_RECS + 0x30 * 0xC)
static const uint32_t og_win_lcd43[12] = { 900, 600, 928, 600, 900, 600, 800, 600, 50, 0, 50, 0 };
static const uint32_t og_win_evf43[12] = { 1280, 960, 1280, 854, 1280, 854, 1136, 854, 72, 0, 72, 0 };

/* runtime MMU pages: E0130000 E0320000 E0360000 E0370000 E0380000 E0390000 E03B0000
 * E07F0000 E0C70000 E0FF0000 E1F10000 (11) in sections E01 E03 E07 E0C E0F E1F (6).
 * Reserved by mmu_reserve_pages_for_patches() (core) for the patch list itself; the core
 * allocates in blocks of a few pages (one ~710 KB block failed once, RESULTS run 42). */

/* the fps24 module patches this word (Canon's 24.00p row caption, stock 0) */
#define OG_FPS24_ROW_WORD 0xE1F107D0

#endif
