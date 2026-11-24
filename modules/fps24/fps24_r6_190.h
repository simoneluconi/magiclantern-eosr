/** \file
 * fps24, R6 1.9.0 values. Sources: research/opengate/fps24/FPS24.md sections 3.1 and 7
 * (static analysis; every patch assembled at its address, stock bytes checked against the
 * ROM dump and the builder emulated stock vs patched over 1728 input cases by asm24.py)
 * and the camera tests of RESULTS.md (row, toggle, store and restore after a reboot).
 */
#ifndef _fps24_r6_190_h_
#define _fps24_r6_190_h_

struct f24_word { uint32_t addr, stock, val; uint8_t insn; const char * what; };

/* P5: the tail of Canon's movie parameter builder E03DB970 (E03DBA38..E03DBAA5, 110 bytes,
 * up to the next function E03DBAA6), rewritten. Stock: +0x30 = 2 when w0 = w1 = 2400, else
 * the video system. New, everything else built as stock:
 *   if (w0 == 2400 && w1 != 2400 && model+0x610 && MovieSize == 5) { w1 = 2400; +0x30 = 2; }
 * so with Canon's 24.00p setting on Enable, 4K UHD 23.98p (full or crop) is published as
 * 24.00p in PROP_MOVIE_CONTROL_PARAM: fps index 9, 60.00 Hz base sync, 24000/1000 time base.
 * FHD has no 24.00 MovieMode row and is left alone. The d02/d03 stores became one strh and
 * the two old stubs went away; no branch enters the range, the literal E03DBC2C is still
 * used. Last word: the high half (E03DBAA6, next function's push) is unchanged */
static const struct f24_word f24_p5[] =
{
    { 0xE03DBA38, 0x1CD0F8D4, 0x1CD0F8D4, 1, "fps24 builder 24.00p" },
    { 0xE03DBA3C, 0x6216F44F, 0x27D0F8D4, 1, "fps24 builder 24.00p" },
    { 0xE03DBA40, 0x42912001, 0x2201B102, 1, "fps24 builder 24.00p" },
    { 0xE03DBA44, 0xF8D4D107, 0x6F16F5B1, 1, "fps24 builder 24.00p" },
    { 0xE03DBA48, 0x42911CD4, 0xF8D4D10F, 1, "fps24 builder 24.00p" },
    { 0xE03DBA4C, 0x2102D103, 0x428B3CD4, 1, "fps24 builder 24.00p" },
    { 0xE03DBA50, 0x1D00F884, 0xF8D6D00A, 1, "fps24 builder 24.00p" },
    { 0xE03DBA54, 0xF8D4E004, 0xF8D334FC, 1, "fps24 builder 24.00p" },
    { 0xE03DBA58, 0xB1F117D0, 0xB1333610, 1, "fps24 builder 24.00p" },
    { 0xE03DBA5C, 0x0D00F884, 0x3CD8F894, 1, "fps24 builder 24.00p" },
    { 0xE03DBA60, 0x0D01F884, 0xD1022B05, 1, "fps24 builder 24.00p" },
    { 0xE03DBA64, 0x14A4F8D6, 0x1CD4F8C4, 1, "fps24 builder 24.00p" },
    { 0xE03DBA68, 0x12CCF8D1, 0xF8842202, 1, "fps24 builder 24.00p" },
    { 0xE03DBA6C, 0xD0172902, 0x20012D00, 1, "fps24 builder 24.00p" },
    { 0xE03DBA70, 0x7D02F884, 0x0D01F884, 1, "fps24 builder 24.00p" },
    { 0xE03DBA74, 0x0F00F1B9, 0x14A4F8D6, 1, "fps24 builder 24.00p" },
    { 0xE03DBA78, 0x7D03F884, 0x12CCF8D1, 1, "fps24 builder 24.00p" },
    { 0xE03DBA7C, 0x2234D00A, 0xBF182902, 1, "fps24 builder 24.00p" },
    { 0xE03DBA80, 0x46684659, 0xF8A44638, 1, "fps24 builder 24.00p" },
    { 0xE03DBA84, 0xF9C8F3FE, 0xF1B90D02, 1, "fps24 builder 24.00p" },
    { 0xE03DBA88, 0xD0032800, 0xD0090F00, 1, "fps24 builder 24.00p" },
    { 0xE03DBA8C, 0x48674651, 0x46592234, 1, "fps24 builder 24.00p" },
    { 0xE03DBA90, 0xFCD4F338, 0xF3FE4668, 1, "fps24 builder 24.00p" },
    { 0xE03DBA94, 0xE8BDB00D, 0xB118F9C1, 1, "fps24 builder 24.00p" },
    { 0xE03DBA98, 0xF8848FF0, 0x48644651, 1, "fps24 builder 24.00p" },
    { 0xE03DBA9C, 0xE7DF7D00, 0xFCCEF338, 1, "fps24 builder 24.00p" },
    { 0xE03DBAA0, 0x0D02F884, 0xE8BDB00D, 1, "fps24 builder 24.00p" },
    { 0xE03DBAA4, 0xE92DE7E6, 0xE92D8FF0, 1, "fps24 builder 24.00p" },
};

/* P4n: the camera controller's 0x8004005C request handler E06B6FA6 (its call was removed in
 * 1.9.0, a nop.w) stores the value in Canon's slot model+0x610 (backed up and restored at
 * boot, reset by Clear settings), then runs the movie parameter builder E03DB970 (with P5
 * it consumes the setting; it posts PROP_MOVIE_CONTROL_PARAM only if it changed). No direct
 * post of 0x8004005C (FPS24.md P4n; P4 adds bl E0714F94 at E06B6FCE): Canon posts the
 * stored value at the next boot. The 0x80000025 stub after it stays equivalent; E06B6FC0
 * (a shared branch target) unchanged */
static const struct f24_word f24_p4n[] =
{
    { 0xE06B6FA4, 0x4601E40E, 0x6801E40E, 1, "fps24 CC store" },
    { 0xE06B6FA8, 0xB5104827, 0xB5104827, 1, "fps24 CC store" },
    { 0xE06B6FAC, 0x23012400, 0x68806800, 1, "fps24 CC store" },
    { 0xE06B6FB0, 0x68004622, 0x42FCF243, 1, "fps24 CC store" },
    { 0xE06B6FB4, 0xF3AF6880, 0xF8C25882, 1, "fps24 CC store" },
    { 0xE06B6FB8, 0xF05D8000, 0x24001610, 1, "fps24 CC store" },
    { 0xE06B6FBC, 0x4620F90B, 0xE0032200, 1, "fps24 CC store" },
    { 0xE06B6FC0, 0x4601E40E, 0xB510E40E, 1, "fps24 CC store" },
    { 0xE06B6FC4, 0xB5104820, 0xE0062400, 1, "fps24 CC store" },
    { 0xE06B6FC8, 0x23012400, 0xF5242301, 1, "fps24 CC store" },
    { 0xE06B6FCC, 0x68004622, 0xF3AFFCD1, 1, "fps24 CC store" },
    { 0xE06B6FD0, 0xF3AF6880, 0xBF008000, 1, "fps24 CC store" },
    { 0xE06B6FD4, 0xF05D8000, 0xF05DBF00, 1, "fps24 CC store" },
};

/* P3: SET on the row (E0B7DD86 case 0x15, a nop.w in 1.9.0): Canon's getter E07A99CA, flip,
 * Canon's setter E07A99B6 (GUI cache + request 0x8004005C), dialog refresh E0B7D02A, then
 * the handler's common exit E0B7DDBE */
static const struct f24_word f24_p3[] =
{
    { 0xE0B7DF00, 0xF3AF4668, 0xFD63F42B, 1, "fps24 row SET" },
    { 0xE0B7DF04, 0x98008000, 0x0001F080, 1, "fps24 row SET" },
    { 0xE0B7DF08, 0xD1F32801, 0xFD55F42B, 1, "fps24 row SET" },
    { 0xE0B7DF0C, 0x200068A9, 0xF88DF7FF, 1, "fps24 row SET" },
    { 0xE0B7DF10, 0xF7D7466A, 0xF7D7E755, 1, "fps24 row SET" },
};

/* P2: the row's value text, Set24P E0B7CE62: Disable (string 0x17) / Enable (0x16), the
 * strings of the HFR row (stock: always 0, a blank) */
static const struct f24_word f24_p2[] =
{
    { 0xE0B7CE64, 0xB1382400, 0xB1382417, 1, "fps24 row value" },   /* movs r4,#0 -> #0x17 */
    { 0xE0B7CE68, 0xD0052801, 0xE0052416, 1, "fps24 row value" },   /* cmp; beq -> movs r4,#0x16; b */
};

/* P1: the row's caption in the Movie rec. quality row table E1F107C4 (item 0x15). 0 hides
 * the row; 0x30B is the string "24.00p" (all 29 languages) */
static const struct f24_word f24_p1[] =
{
    { 0xE1F107D0, 0x00000000, 0x0000030B, 0, "fps24 row caption" },
};

/* Canon's stored setting: model+0x610, model = *(*(*(0x10C28)+8)+0x34FC) */
#define F24_CC_PTR        0x10C28
#define F24_MODEL_OFF     0x34FC
#define F24_STORE_OFF     0x610

/* ---- sensor timing table, row 15 (4K UHD and 4K crop, LvCapMode 0x1B / 0x33) ----
 * Live DRAM copy of ROM1 F072A11C, rebuilt at every boot (FPS24.md 1.4, 7.2).
 * u16 cell = TABLE + off + 15*0x20 + 2*col; cartridge u32 = TABLE + 0x1180 + 15*0x40 + 4*col.
 * Column = Canon's fps index: 4 = 23.98p (stock, checked), 9 = 24.00p (all 0 in the stock
 * table, filled here). 24.00p values: FL = floor(27 MHz sync period 1,125,000 / HD 1140) =
 * 986, RPS = FL - 23 as in column 4; the others are column 4's (same sensor mode). */
#define F24_TIMING_INFO   0x11EA8       /* +0x20 timing table pointer, +0x24 fps index */
#define F24_TIMING_TABLE  0x42BAA11C
#define F24_ROW           15
#define F24_COL_2398      4
#define F24_COL_2400      9
#define F24_CART          0xFFFF        /* off of the u32 cartridge cell (own layout) */
struct f24_cell { uint16_t off; uint8_t size; uint32_t col4, val; const char * what; };
static const struct f24_cell f24_cells[] =
{
    { 0x000, 2, 987, 986,       "FL" },
    { 0x280, 2, 655, 655,       "SetP" },
    { 0x500, 2, 751, 751,       "ROD" },
    { 0x780, 2, 964, 963,       "RPS" },
    { 0xA00, 2, 1140, 1140,     "HD ticks" },
    { 0xC80, 2, 42, 42,         "HD us" },
    { 0xF00, 2, 1, 1,           "flag" },
    { F24_CART, 4, 0x6FF0, 0x6FF0, "Cartridge" },
};

#define F24_IMGCTRL       0x17658       /* +0x10 sensor VD (read only, shown in the menu) */

/* runtime MMU pages E1F10000, E0B70000, E06B0000, E03D0000 (4) in sections E1F, E0B, E06,
 * E03 on top of the port's 6 runtime pages / 6 L2 tables. The port's runtime pages are in
 * sections E07 E05 E03 E04 E0D (E03A0000 shares E03 with P5), so 3 more tables cover the
 * union in every case (platform/R6.190/internals.h: max 19 pages, 12 L2 tables) */
#define F24_PAGES         (6 + 4)
#define F24_L2            (6 + 3)

/* the opengate module is in when its first HEVC level word (30p ALL-I, stock 52) is patched */
#define F24_OPENGATE_WORD 0xE1F1E7CC
#define F24_OPENGATE_STOCK 52

#endif
