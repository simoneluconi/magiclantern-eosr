/** \file
 * alli, R6 1.9.0 values. Source: research/all-i-feasibility.md section 7 (static analysis:
 * the size-kind switch and ConvertParamToMovieKind emulated for every size / rate / GOP /
 * quality, every layout decoded from the ROM dump, whole-ROM pointer scans).
 */
#ifndef _alli_r6_190_h_
#define _alli_r6_190_h_

/* Movie rec. quality (DlgMnMovieSizeMultiStream.c): at each open E0B80384 picks a cell
 * layout from the selector table E1F13B74 (literal E0B807B4; 6 rows of 0x1C bytes, the
 * layout pointer at +0x18). Keys: PAL, HFR, movie crop (PROP 0x80020054), spatial
 * oversampling. Each layout is two variants of 0x3F4 bytes (the second when
 * E0B1918C(0xF) == 1, the 10-bit state: no Light cells): word 0 = type, then up to 36 cells
 * of 7 words, ended by kind 265. Each layout pointer has no other reference in the ROM */
#define AI_SEL_LITERAL      0xE0B807B4  /* ldr literal in E0B80384 */
#define AI_SEL_TABLE        0xE1F13B74
#define AI_LAYOUT_SIZE      0x3F4       /* one variant */
#define AI_LAYOUT_VARIANTS  2
#define AI_MAX_CELLS        36          /* dialog item slots (index table E1F13FA4) */
#define AI_KIND_END         265         /* MAX_MOVIE_SIZE_KIND: end of list / no proxy */
#define AI_LAYOUT_TYPE      3

/* cell: {kind, proxy kind (265), size icon, fps icon, compression caption, size/fps caption, 0} */
struct ai_cell { uint32_t kind, proxy, size_icon, fps_icon, caption, size_caption, zero; };

/* compression captions (string resources, all 29 languages) */
#define AI_CAP_ALLI         0x2F9       /* "For editing (ALL-I)" */
#define AI_CAP_IPB          0x2FA       /* "Standard (IPB)" */
#define AI_CAP_LIGHT        0x2FB       /* "Light (IPB)" */

/* GetBmpIconIdForCompression E0B7FB38: {caption, icon} pairs; Canon's table already has
 * ALL-I (pair 2, icon 0 as IPB) */
#define AI_ICON_TABLE       0xE1F13CAC
#define AI_ICON_ALLI_PAIR   2

/* the 4 normal layouts (HFR layouts E1F12BA4 / E1F1338C stay as they are) */
struct ai_layout
{
    uint32_t sel_word;                  /* selector row + 0x18: the patched pointer */
    uint32_t stock;                     /* Canon's layout */
    const char * what;
    uint8_t n[AI_LAYOUT_VARIANTS];      /* stock cell count per variant */
    uint16_t kinds[AI_LAYOUT_VARIANTS][12];  /* stock cell kinds, in order */
};

static const struct ai_layout ai_layouts[] =
{
    { 0xE1F13B8C, 0xE1F10C04, "alli NTSC",      { 12, 6 },
      { { 185, 187, 55, 56, 66, 67, 77, 78, 83, 84, 92, 93 }, { 185, 55, 66, 77, 83, 92 } } },
    { 0xE1F13BA8, 0xE1F113EC, "alli PAL",       { 8, 4 },
      { { 186, 188, 58, 59, 80, 81, 86, 87 }, { 186, 58, 80, 86 } } },
    { 0xE1F13BC4, 0xE1F11BD4, "alli NTSC crop", { 12, 6 },
      { { 154, 159, 156, 161, 158, 163, 171, 174, 173, 176, 179, 180 }, { 154, 156, 158, 171, 173, 179 } } },
    { 0xE1F13BE0, 0xE1F123BC, "alli PAL crop",  { 8, 4 },
      { { 155, 160, 157, 162, 172, 175, 177, 178 }, { 155, 157, 172, 177 } } },
};

/* the patched range, selector rows 0..3 from the first pointer to the last (0x58 bytes):
 * {PAL, GUI+0x4C, GUI+0x90, HFR, crop, oversampling, layout} per row */
#define AI_SEL_FIRST 0xE1F13B8C
static const uint32_t ai_sel_stock[] =
{
                                    0xE1F10C04, /* row 0: NTSC */
    1, 0, 0, 0, 0, 0,               0xE1F113EC, /* row 1: PAL */
    0, 0, 0, 0, 1, 0,               0xE1F11BD4, /* row 2: NTSC crop */
    1, 0, 0, 0, 1, 0,               0xE1F123BC, /* row 3: PAL crop */
};

/* IPB kind -> ALL-I kind of the same size and rate, up to 30p (GUI kind switch E0AFAF7A:
 * {rate, MovieSize, GOP, MP4, quality}; ConvertParamToMovieKind E0AFB97A maps each ALL-I
 * record back to these kinds, in the same family: 54..93 without crop, 149..180 with crop).
 * 50 / 59.94p get no ALL-I cell (940 Mbps in 4K) */
struct ai_pair { uint16_t ipb, alli; };
static const struct ai_pair ai_pairs[] =
{
    {  55,  54 },   /* UHD 29.97 */
    {  58,  57 },   /* UHD 25 */
    {  66,  65 },   /* UHD 23.98 */
    {  83,  82 },   /* FHD 29.97 */
    {  86,  85 },   /* FHD 25 */
    {  92,  91 },   /* FHD 23.98 */
    { 156, 151 },   /* crop: UHD 29.97 */
    { 157, 152 },   /* UHD 25 */
    { 158, 153 },   /* UHD 23.98 */
    { 173, 168 },   /* FHD 29.97 */
    { 177, 169 },   /* FHD 25 */
    { 179, 170 },   /* FHD 23.98 */
};

/* Canon's GUI calls (Thumb):
 * - GetMovieSizeKind E0AFCAFC(&kind): ConvertParamToMovieKind of the GUI's movie parameter;
 * - SetMovieSizeKind E0AFB956(kind): what SET in the dialog does (E0B80638): the record
 *   (E0AFAF7A) -> GUI setter E07AE8E4 -> prop_request_change(0x800000C7), then the crop
 *   setting of the kind's family (E0AFE8F4 -> E07ABF90);
 * - GetMovieParam E0AA54EA(&rec16): the GUI's cached record {rate, cap, size, GOP,
 *   container, quality, ...} */
#define AI_GET_KIND         0xE0AFCAFD
#define AI_SET_KIND         0xE0AFB957
#define AI_GET_PARAM        0xE0AA54EB

/* Canon's player refuses clips above 470.2 Mbps (ChkBitRate in CheckMovieFileInfo, the only
 * load at E04EBC34); 4K ALL-I is 470 Mbps video plus audio */
#define AI_PLAYER_BITRATE       0xE04EC01C
#define AI_PLAYER_BITRATE_STOCK 470200000
#define AI_PLAYER_BITRATE_NEW   1000000000

/* the opengate module is in when its first HEVC level word (30p ALL-I, stock 52) is patched */
#define AI_OPENGATE_WORD    0xE1F1E7CC
#define AI_OPENGATE_STOCK   52

#endif
