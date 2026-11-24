/** \file
 * fps24, R6 1.9.0 values. Sources: research/opengate/fps24/FPS24.md section 7 (static
 * analysis, words assembled and checked by fps24/asm24.py and r24_check.py) and the camera
 * tests of RESULTS.md (row, toggle, store and restore after a reboot).
 */
#ifndef _fps24_r6_190_h_
#define _fps24_r6_190_h_

struct f24_word { uint32_t addr, stock, val; uint8_t insn; const char * what; };

/* P1: the row's caption in the Movie rec. quality row table E1F107C4 (item 0x15). 0 hides
 * the row; 0x30B is the string "24.00p" (all 29 languages) */
static const struct f24_word f24_p1[] =
{
    { 0xE1F107D0, 0x00000000, 0x0000030B, 0, "fps24 row caption" },
};

/* P2: the row's value text, Set24P E0B7CE62: Disable (string 0x17) / Enable (0x16), the
 * strings of the HFR row (stock: always 0, a blank) */
static const struct f24_word f24_p2[] =
{
    { 0xE0B7CE64, 0xB1382400, 0xB1382417, 1, "fps24 row value" },   /* movs r4,#0 -> #0x17 */
    { 0xE0B7CE68, 0xD0052801, 0xE0052416, 1, "fps24 row value" },   /* cmp; beq -> movs r4,#0x16; b */
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

/* P4n: the camera controller's 0x8004005C request handler E06B6FA6 (its call was removed in
 * 1.9.0, a nop.w) stores the value in Canon's slot model+0x610 (backed up and restored at
 * boot, reset by Clear settings), then runs the stock movie parameter builder E03DB970,
 * which ignores it: the frame rate is unchanged. No direct post of 0x8004005C. The
 * 0x80000025 stub after it stays equivalent; E06B6FC0 (a shared branch target) unchanged */
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

/* Canon's stored setting: model+0x610, model = *(*(*(0x10C28)+8)+0x34FC) */
#define F24_CC_PTR        0x10C28
#define F24_MODEL_OFF     0x34FC
#define F24_STORE_OFF     0x610

/* runtime pages E1F10000, E0B70000, E06B0000 in 3 sections, on top of the port's 6 boot
 * pages / 6 L2 tables */
#define F24_PAGES         (6 + 3)
#define F24_L2            (6 + 3)

/* the opengate module is in when its first HEVC level word (30p ALL-I, stock 52) is patched */
#define F24_OPENGATE_WORD 0xE1F1E7CC
#define F24_OPENGATE_STOCK 52

#endif
