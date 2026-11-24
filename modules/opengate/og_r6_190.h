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
};
/* SSG D2408280/90/98 are derived by Canon from ROD: never written (RESULTS run 2). */

/* ---- timing table row 15, the column of the movie fps (FPS25.md 1.2) ----
 * u16 cell = TIMING + off + 15*0x20 + 2*col; cartridge u32 = TIMING + 0x1180 + 15*0x40 + 4*col.
 * Column = Canon's fps index: 2 = 29.97p, 3 = 25p, 4 = 23.98p. 25p = the cells of run 36/37
 * (slot-9 line time HD 524, full-height ROD/RPS); 29.97p FL/RPS are Canon's HD-524 29.97
 * values; 23.98p is derived (FL 2149, RPS = FL - 52). */
#define OG_TIMING_TABLE 0x42BAA11C
#define OG_TCELL(off, col)  (OG_TIMING_TABLE + (off) + 15 * 0x20 + 2 * (col))
#define OG_TCART(col)       (OG_TIMING_TABLE + 0x1180 + 15 * 0x40 + 4 * (col))
/* fps100 = Canon's exact rate x 100 (PROP_MOVIE_CONTROL_PARAM word 1). 24.00p is its own
 * fps index 9 (column 9, zero in the stock table, 60.00 Hz base sync; fps24/FPS24.md), not
 * column 4: not supported (2400 is no rate here, and the live check refuses column 9) */
struct og_rate { uint8_t col; const char * name; uint16_t fl_stock, fl, rps_stock, rps, fps100; };
static const struct og_rate og_rates[] =
{
    { 2, "29.97p", 790, 1719, 766, 1667, 2997 },
    { 3, "25p",    947, 2061, 923, 2010, 2500 },
    { 4, "23.98p", 987, 2149, 964, 2097, 2398 },
};
#define OG_T_SETP_STOCK  655        /* +0x280 */
#define OG_T_SETP        627
#define OG_T_ROD_STOCK   751        /* +0x500 */
#define OG_T_ROD         1027
#define OG_T_HD_STOCK    1140       /* +0xA00, HD ticks */
#define OG_T_HD          524
#define OG_T_HDUS_STOCK  42         /* +0xC80, HD us */
#define OG_T_HDUS        19
#define OG_T_FLAG        1          /* +0xF00, check only */
#define OG_T_CART_STOCK  0x6FF0
#define OG_T_CART        0x3371

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
    { 0xE03B71B8, 0x20089000, 0x20079000, 1, "OG enc B3 buffer from pool 7" },
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

/* runtime MMU pages: E0130000 E0320000 E0360000 E0370000 E0380000 E0390000 E03B0000
 * E07F0000 E0C70000 E0FF0000 E1F10000 (11) in sections E01 E03 E07 E0C E0F E1F (6),
 * on top of the 6 pages / 6 L2 tables the port takes at boot. Reserved in small
 * steps: one ~710 KB AllocateMemory block failed once (RESULTS run 42). */
#define OG_BOOT_PAGES   6
#define OG_BOOT_L2      6
static const uint8_t og_reserve_steps[][2] = { { 10, 8 }, { 14, 10 }, { 17, 12 } };

/* Canon's player and 4096x2736 clips (research/opengate/player/PLAYER-2736.md, stage 1).
 * Without P1 the player refuses the clip ("Cannot play back movie"); P1 alone would let
 * it reach the size index, which asserts for 4096 x any height but 2160 (crash), so P1
 * never goes without P2; P2 alone or P1+P2 without P3 overruns the 6 decoded-frame
 * buffers sized for 4096x2160 by 15.9 MiB, so the three are one all-or-nothing set.
 * P3 applies to every clip played while it is in. */
static const struct og_word og_player_words[] =
{
    { 0xE04EB9F8, 0xD11542A1, 0xBF0042A1, 1, "OG player ChkFrameRate any height" },   /* bne -> nop */
    { 0xE04EFB84, 0x2000D005, 0x2000E005, 1, "OG player 4096 x any -> size index 2" }, /* beq -> b */
    { 0xE0401B84, 0x61A82006, 0x61A82004, 1, "OG player 4 decoded frames, not 6" },   /* movs r0,#6 -> #4 */
};

/* pages E04E0000 and E0400000, section E04. The port's 6 boot pages (consts.h: writer
 * E03A E05E E05F and player E04E, all with the 60-min option; time limit E074; heat
 * timer E0D9) are in 5 sections, E03 E04 E05 E07 E0D; the open gate shares E03 and E07
 * and adds E01 E0C E0F E1F. With the 60-min option on, E04 and page E04E0000 are already
 * mapped: 6 + 11 + 1 = 18 pages, 5 + 4 = 9 L2 tables; with it off, at most
 * 2 + 11 + 2 = 15 pages and 2 + 6 + 1 = 9 L2 tables. Both inside 19 / 12 */
/* diagnostic D1 (PLAYER-2736.md 8.4), with P1-P3: 4096 x any height takes the 8K playback
 * path (ORCA working size halved, Rsz mode 3). Camera: P1-P3 alone stop within 0.2 s of the
 * preview start, for H.264 and HEVC, level 5.1 or 6.1; 4096x2160 plays stock */
static const struct og_word og_player_d1 =
    { 0xE04EFB90, 0x2402FDC1, 0x2401FDC1, 1, "OG player test: 8K resize path" };  /* movs r4,#2 -> #1 */

/* bisection (PLAYER-2736.md 10): camera, D1 decodes 3 frames of 2736 rows in ~77 ms, then
 * waits 500 ms and fails (80000011): the decoder is fine, the ORCA convert/resize is not.
 * X1: the convert (mvpCnvMp4) returns an error at once; X2: the resize (mvpRszMp4) does */
static const struct og_word og_player_x1 =
    { 0xE04052D4, 0x460DB570, 0x47702001, 1, "OG player test: no ORCA convert" };  /* movs r0,#1; bx lr */
static const struct og_word og_player_x2 =
    { 0xE0405204, 0x47F0E92D, 0x47702001, 1, "OG player test: no ORCA resize" };   /* movs r0,#1; bx lr */

/* Canon's hidden "24.00p" row in Movie rec. quality (fps24/FPS24.md 7, steps 1-2 of 7.5):
 * GUI only, nothing reaches the camera controller (its request handler is still the stock
 * nop.w), so nothing is stored and the sensor never changes. Words from fps24/asm24.py. */
static const struct og_word og_r24_p1[] =    /* the row's caption: 0 = hidden, 0x30B = "24.00p" */
{
    { 0xE1F107D0, 0x00000000, 0x0000030B, 0, "OG 24p row caption" },
};
static const struct og_word og_r24_p2[] =    /* value text Disable (0x17) / Enable (0x16) */
{
    { 0xE0B7CE64, 0xB1382400, 0xB1382417, 1, "OG 24p row value" },
    { 0xE0B7CE68, 0xD0052801, 0xE0052416, 1, "OG 24p row value" },
};
static const struct og_word og_r24_p3[] =    /* SET: get, flip, set (E07A99CA/E07A99B6), refresh */
{
    { 0xE0B7DF00, 0xF3AF4668, 0xFD63F42B, 1, "OG 24p row SET" },
    { 0xE0B7DF04, 0x98008000, 0x0001F080, 1, "OG 24p row SET" },
    { 0xE0B7DF08, 0xD1F32801, 0xFD55F42B, 1, "OG 24p row SET" },
    { 0xE0B7DF0C, 0x200068A9, 0xF88DF7FF, 1, "OG 24p row SET" },
    { 0xE0B7DF10, 0xF7D7466A, 0xF7D7E755, 1, "OG 24p row SET" },
};
/* pages E1F10000 (shared with the open gate) and E0B70000, sections E1F and E0B */

#define OG_PLAYER_PAGES   2
#define OG_PLAYER_L2      1
#define OG_ALL_PAGES      19
#define OG_ALL_L2         12

#endif
