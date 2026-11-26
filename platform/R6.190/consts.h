/*
 *  EOS R6 1.9.0 consts
 *
 *  Addresses found in the 1.9.0 ROM. Values marked "R5, unverified"
 *  are copied from R5.152 and not yet confirmed on R6.
 */

// Device::LEDDef.c: LED 0 is port 0xC2 (0xE01F7818); the GPIO writer 0xE0891AE4
// uses (port & ~3) + 0xD2239000. On / off values from 0xE01F7546 / 0xE01F75E6.
#define CARD_LED_ADDRESS            0xD22390C0
#define LEDON                       0x24D0002
#define LEDOFF                      0x24C0003

// Identical positions to R6.150 and R5.152; verified in 1.9.0.
#define BR_DCACHE_CLN_1     0xE0100058   /* first call to dcache_clean, before cstart */
#define BR_ICACHE_INV_1     0xE0100062   /* first call to icache_invalidate, before cstart */
#define BR_DCACHE_CLN_2     0xE010008E   /* second call to dcache_clean, before cstart */
#define BR_ICACHE_INV_2     0xE0100098   /* second call to icache_invalidate, before cstart */
#define BR_BR_CSTART        0xE01000AE   // indirect branch to cstart (ldr r0,=0xE0100023; bx r0);
                                         // the first branch is absolute to original, we must patch
#define BR_BZERO32          0xE0100138   /* called from cstart */
#define BR_CREATE_ITASK     0xE010019A   /* called from cstart */

// firmware_entry and cstart are contiguous: 0xE0100000 - 0xE010020F including
// the per-CPU init 0xE01001D0 and literal pools. No CSTART_LEN needed.
#define FIRMWARE_ENTRY_LEN 0x800

/* "Malloc Information", get_malloc_info 0xE037C234 */
#define MALLOC_STRUCT_ADDR 0xFCF48

/* DryOS assert 0xE08D7714 reads the handler from 0x4000, setter 0xE08D7756 */
#define DRYOS_ASSERT_HANDLER        0x4000

/* For CONFIG_SGI_HANDLERS (not enabled yet). The IRQ dispatcher (0xE022E0CE)
 * calls entry [GIC id] of the table that 0x4008 points to; entries are
 * {func, arg}, an empty one gives "Illegal GIC inter...". Same as 850D.
 * 0x4008 is initialised data (0x000FBA34, copied by firmware_entry from ROM
 * 0xE1FBE8B0 to 0x4000), so it is valid before ML runs. At boot Canon
 * registers SGI 0xA, 0xB, 0xC (RPC) and 0xD (0xE0142554, 0xE01426C0,
 * 0xDFFC7BEE, 0xDFFC7E02); slots 0-9, which ML searches, are not checked on
 * the camera yet. */
#define DRYOS_SGI_HANDLERS_PTR      0x4008

/* For CONFIG_MMU (not enabled yet): Canon's MMU table template in ROM,
 * 0x4900 bytes in the layout src/mmu_utils.c expects. The bootloader
 * (0xE000D728 / 0xE000D75C) copies it from 0xE0000000 to 0xDFFC0000 and
 * fixes the TTBR0 L1 entries at +0x4800 / +0x4880; the live table read on
 * the camera differs from the template only there. We copy the template,
 * like M50 / 850D, not the live table: it is what Canon copies too, and it
 * cannot change under us (Canon's flash code temporarily unmaps ROM in the
 * live table, and patch_mmu.c reads memory flags from this table every
 * time a patch is applied). */
#define CANON_ORIG_MMU_TABLE_ADDR   0xE0000000

// GetGUIMode 0xE079A692: ldr r0,=0x8D00; ldr r0,[r0,#0x70]
#define CURRENT_GUI_MODE            (*(int*)0x8D70)

/**
 * New GUI mode numbering scheme, like R5 (IDLEHandler 0xE0BD3880:
 * START_PLAY_MODE -> SetGUIMode(0x2002), START_MENU_MODE -> SetGUIMode(0x2003))
 */
#define GUIMODE_PLAY 0x2002
#define GUIMODE_MENU 0x2003
// The ML menu opens over Canon's MENU, also from LV, as on RP.160.
// The R5 value for LV, 0xC048, is Canon's Q screen on R6 (name table
// 0xE1C585BC, low byte 0x48 = START_UNAVI_MODE; movw + SetGUIRequestMode
// at 0xE07543B4, compared in the PRESS_UNAVI handler 0xE0790296).
// While recording, Canon does not switch to its MENU (seen on camera:
// the GUI mode stayed 0 and the ML menu closed after ~0.5 s), so, as on
// 5D3 / 50D / 60D..., ML asks for no Canon dialog then and draws its
// menu over LiveView.
#define GUIMODE_ML_MENU (RECORDING ? 0 : GUIMODE_MENU)

#define CANON_SHUTTER_RATING 300000 // Canon's official rating for R6

// PROP_EFIC_TEMP is TempSvc sensor "A" on R6 (see property.h). TempSvc
// (0xE099B132) publishes ((t16 + 8) >> 4) + 128, t16 in 1/16 deg C.
#define EFIC_CELSIUS ((int)efic_temp - 128)
#define EFIC_TEMP_HELP "Canon's temperature sensor A (I2C). Location not known."

// Movie heat timer (FEATURE_MOVIE_HEAT_TIMER, src/heat_timer.c).
// Canon's time-based heat model (0xE04E902A, once per second) keeps a
// budget from 0 to 0xFFFF and drains it by 0xFFFF / seconds (integer
// division) per second. The seconds come from this ROM table, read in
// place (literal pools 0xE04E8DC8, 0xE04E9378, 0xE04E9794; no RAM copy).
// Each row (0x2C bytes) starts with a 5-byte mode key (state, size
// class, frame rate class) matched by 0xE04E8CE6. Two 8-byte parts
// follow: standby (+0x08) and recording (+0x10, used while
// PROP_SHOOTING_TYPE is 4). Each part has the seconds (32 bit), then a
// "stop when empty" byte and, at +2, a "drain" byte. Only the seconds
// are patched. Keys, flags and the sensor thresholds stay untouched.
#define HEAT_TABLE_ADDR         0xE0D9CEA4
#define HEAT_TABLE_ROWS         24
#define HEAT_TABLE_ROW_SIZE     0x2C
#define HEAT_TABLE_STANDBY      0x08
#define HEAT_TABLE_RECORDING    0x10

// Canon's seconds per row, {standby, recording}, checked before patching.
// Rows 2 and 3 (keys 13 04 0A / 13 04 09) are most likely 4K 50/60p and
// 4K <= 30p: they match Canon's published 30 / 40 min limits. Row 1
// (key 16) is used while PROP_GUI_STATE is not 0, 6, 9 or 10 (0xE04E83D4),
// e.g. in a menu.
#define HEAT_TABLE_SECONDS { \
    {  4500,  4500 }, { 33000, 33000 }, {  1800,  1798 }, {  3600,  2400 }, \
    {  4800,  3000 }, {  3900,  2700 }, {  7500,  7500 }, {  2700,  2100 }, \
    {  3900,  3300 }, {  2700,  2100 }, {  3900,  3300 }, {  4500,  1798 }, \
    {  3600,  2400 }, {  9600,  3000 }, {  7500,  2700 }, {  7500,  7500 }, \
    {  4200,  2100 }, {  3900,  3300 }, {  4200,  2100 }, {  3900,  3300 }, \
    {  3900,  2700 }, {  2700,  2100 }, { 12000, 12000 }, { 23100, 23100 }, \
}

// Largest patched value. 0xE04E95D6 and 0xE04E965C compute the remaining
// time as budget * seconds / 0xFFFF in 32 bits, which stays below 2^31
// up to 0x7FFF s. The drain per second is then at least 2, never 0.
// Rows already above it (33000 s) are left as they are.
#define HEAT_TABLE_SECONDS_MAX  0x7FFF

// DispOperator_PropertyMasterSetDisplayTurnOffOn 0xE0751460 caches the requested
// state here (1 = on). Not yet confirmed on the camera.
#define DISPLAY_IS_ON               (*(int *)0xEF48)

/*
 * Output colour matrix of the LCD (n = 0) and EVF (n = 1), for
 * FEATURE_DISPLAY_RED_MATRIX. Addresses, standard values and the red
 * values were found and tested on this camera by andrewpoison (8e669dc27,
 * "astro red Full"); the rest is from the 1.9.0 ROM.
 *
 * Six words, a 3x3 matrix YCbCr -> G, B, R in s.12 fixed point (15-bit
 * two's complement), two coefficients per word, written by Canon in
 * 0xE0214338 (low half first):
 *   +0x48 = G.Y | G.Cb << 16   +0x4C = G.Cr
 *   +0x50 = B.Y | B.Cb << 16   +0x54 = B.Cr
 *   +0x58 = R.Y | R.Cb << 16   +0x5C = R.Cr
 * The standard set is BT.601 (1.0, -0.344, -0.714 / 1.0, 1.772, 0 /
 * 1.0, 0, 1.402); it is the only matrix in the ROM's display parameter
 * blocks (0xE0FC9E20, 0xE1C8871C and three more).
 */
#define DISP_MATRIX_ADDR(n)     (0xD282B048 + 0x10000 * (n))
#define DISP_MATRIX_STD         { 0x7A7E1000, 0x00007493, 0x1C5A1000, 0x00000000, 0x00001000, 0x0000166F }
#define DISP_MATRIX_ONE         0x1000      /* 1.0 */
#define DISP_MATRIX_CPU         0           /* Canon's display tasks run on CPU0 (andrewpoison, 8e669dc27) */

/* Output status word, readable at any time (8e669dc27). Canon writes the
 * matrix only if (status & 0x30) (0xE06B2EA4); 0x37 = clock and output on,
 * as read while the output shows an image (8e669dc27). */
#define DISP_OUT_STATUS(n)      (0xD2823000 + 0x10000 * (n))
#define DISP_OUT_RUNNING        0x37

/* Canon's matrix setter 0xE01B8882 (IRQs off, as cli()) writes the six words
 * and then calls 0xE020902C, which returns without the latch write to
 * 0xD282302C only when [DISP_UPDATE_MODE(n)] != 1 and the state of the
 * output's channel (0xE0209DCA) is 2. In that state Canon's own matrix
 * update is a plain store, as ours; andrewpoison wrote only there. */
#define DISP_UPDATE_MODE(n)     (0x1615C8 + 4 * (n))
#define DISP_OUT_CHANNEL(n)     (0x2F76C + 4 * (n))
#define DISP_CHANNEL_STATE(ch)  (0x1E1B4 + 4 * ((ch) != 0))
#define DISP_CHANNEL_RUNNING    2

#define GMT_FUNCTABLE               0xE0CD1B00           // from gui_main_task 0xE04DE3F8 (memcpy 0x1C)
#define GMT_NFUNCS                  0x7                  // size of table above

// eventprocs lvae_set* from 0xE072A842 on; all field offsets identical to R5
#define LVAE_STRUCT                 0x120334
#define CONTROL_BV      (*(uint16_t*)(LVAE_STRUCT+0x28)) // via "lvae_setcontrolbv"
#define CONTROL_BV_TV   (*(uint16_t*)(LVAE_STRUCT+0x42)) // via "lvae_setcontrolaeparam"
#define CONTROL_BV_AV   (*(uint16_t*)(LVAE_STRUCT+0x44)) // via "lvae_setcontrolaeparam"
#define CONTROL_BV_ISO  (*(uint16_t*)(LVAE_STRUCT+0x46)) // via "lvae_setcontrolaeparam"
#define CONTROL_BV_ZERO (*(uint16_t*)(LVAE_STRUCT+0x48)) // via "lvae_setcontrolaccumh"
#define LVAE_DISP_GAIN  (*(uint16_t*)(LVAE_STRUCT+0x74)) // via "lvae_setdispgain"
#define LVAE_MOV_M_CTRL (*(uint8_t* )(LVAE_STRUCT+0x24)) // via "lvae_setmoviemanualcontrol"

/*
 * LvInfoToggle_Update 0xE08C1B90 ("LvInfoToggle_Update ToggleIndex:%d -> %d").
 * R6 has 9 states (0..8) and no "TURNOFF" string. Seen on camera: on the
 * LCD 3 is the clean INFO screen, as on R5; the EVF has its own INFO cycle
 * here, 0 nothing over the image (exposure in the strip below it),
 * 1 minimal info, 2 full info.
 */
#define LV_OVERLAYS_MODE MEM(0x20F70)
extern int lv_display_is_evf(void);     /* faceyuv.c: the EVF alone is on */
#define LV_OVERLAYS_MODE_CLEAN (lv_display_is_evf() ? 0 : 3)

/* Beeps (CONFIG_BEEP_SE, src/beep.c). "Speaker Volume" 1..5 scales the
 * samples. SE_BEEP_OBJ: pointer to Canon's sound-effect object, set by
 * SE_Initialize 0xE085DFB4; +0x48 PCM channel state, 3 = idle
 * (SE_PlayPWMWait 0xE085E380 polls it). */
#define ASIF_MAX_VOL 5
#define SE_BEEP_OBJ  0xE628

// AeProcessSetting 0xE0725E9A; ISOMin via " >> ISOMin:%d" (LVAE_STRUCT - 0x48, as on R5)
#define LVAE_ISO_STRUCT 0x1202EC
#define LVAE_ISO_MIN    (*(uint8_t* )LVAE_ISO_STRUCT + 0x0E ) // via string: ISOMin:%d

/*
 * DISP_VRAM pointers are over 32 bits on DIGIC X (IBus addresses), see R5.
 * LV buffers are not usable yet.
 */
#define YUV422_LV_BUFFER_1   0x0
#define YUV422_LV_BUFFER_2   0x0
#define YUV422_LV_BUFFER_3   0x0

#define YUV422_LV_BUFFER_DISPLAY_ADDR 0x0
#define YUV422_LV_PITCH               1024       // depends on display type

#define YUV422_HD_BUFFER_DMA_ADDR 0x0 // it expects this to be shamem_read(some_DMA_ADDR)

/* ML's LV image instead: Canon's FaceYuv buffer, the whole frame; its size
 * follows the LV mode (CONFIG_FACEYUV_LV, faceyuv.c) */

/* CONFIG_LV_DISPLAY_WINDOW: Canon's LCD / EVF window records, .data
 * 0x245C8 (ROM image 0xE1FDEE78), 0x30 bytes each: panel w,h; VRAM w,h;
 * area w,h (the box the resizer fills); visible w,h; visible offset x,y in
 * the area; position x,y in the VRAM. Lookup 0xE080BEC4 (key dev << 12 |
 * sub << 8 | mode << 4 | aspect) returns a pointer into it, read at each
 * display setup. Records: 0x0 LCD 3:2, 0x4 LCD movie, 0xA EVF 3:2, 0xE EVF
 * movie (all with area = visible); 0x2 / 0xC LCD / EVF display aspect 2,
 * the stills 4:3 view, which opengate 4:3 selects for its frame. */
#define LV_DISP_WIN_TABLE         0x245C8
#define LV_DISP_WIN_ROM           0xE1FDEE78
#define LV_DISP_WIN_RECORDS       { 0x0, 0x4, 0xA, 0xE }
#define LV_DISP_WIN_FRAME_RECORDS { 0x2, 0xC }

/* WRONG! */
#define HALFSHUTTER_PRESSED         0

/* CONFIG_SW12_VIA_CC: Canon's shutter switch path, read only by ML.
 * "cc" is Canon's CameraConductor object; CC_SW1_CBR / CC_SW2_CBR (stubs.S)
 * write its switch bytes and post its switch events. From andrewpoison's
 * port (227138ec7, r6_interval.h), rechecked in the 1.9.0 ROM. */
#define CC_SW_CC_PTR        0x10AB8 // cc as the two callbacks read it, without a NULL check
#define CC_SW_CC_PTR_POST   0x68C4  // cc as their event post reads it ([0x68C0+4], 0xE0287C2E)
#define CC_SW_CC_PTR_WRAP   0x108D4 // cc as the switch wrapper reads it ([0x108D0+4], 0xE06D30AE)
#define CC_SW_MGR_PTR       0x1E444 // MPU switch manager ([0x1E440+4]); [mgr] = callbacks, SW1 at +0, SW2 at +4
#define CC_SW_HACK_QUEUE    0x1E43C // != 0: Canon diverts switch events into a queue (0xE0376E1E)
#define CC_SELF_RELEASE_CB  0x108A8 // != 0: Canon's own self-release macro is running ([0x108A0+8], 0xE031DCF8)
#define CC_EVENT_FLAG       0x008   // [cc+]: event flag of the post; 0 or odd = Canon asserts
#define CC_SW_EVENTS_PEND   0x018   // [cc+]: class-2 events pending (the post ORs them in)
#define CC_SW_EVENTS_WORK   0x050   // [cc+]: class-2 events being handled
#define CC_SW_EVENT_MASK    0x3C00  // SW1 on 0x400, SW1 off 0x800, SW2 on 0x1000, SW2 off 0x2000
#define CC_SW1_BYTE         0x0DF   // [cc+]: SW1 held; the SW1 callback stores its status here
#define CC_SW2_BYTE         0x0E0   // [cc+]: SW2 held; the SW2 callback stores its status here
#define CC_SW_GATE          0x2E0   // [cc+]: != 0: the switch wrapper drops switch events
// How far a release is (sw12_hold_sw2() in src/lens.c): CC state, set by
// CC_ChangeCurrentCCState 0xE07020D8, and the release sequence flag, set to 1
// by ActReleaseSequenceStart (0xE031DD14 / 0xE031DD18), cleared by ActRelSeqComp
#define CC_STATE            0x294   // [cc+]
#define CC_RELSEQ_CC        0xB54   // [cc+]: release sequence running
#define CC_RELSEQ           0x108AC // [0x108A0+0xC]: the same flag, ActRelSeq's copy
// CC states in andrewpoison's camera traces: 1 / 3 idle, 2 SW1 taken,
// 4 release accepted (SW2 off here = RELEASE_STOP), 5 SHOOT_START,
// 6 EXPOSURE_END, 12 bulb
#define CC_STATE_SHOOT_START  5
#define CC_STATE_EXPOSURE_END 6
#define CC_STATE_BULB         12

#define NUM_PICSTYLES 11 // R5, unverified

#define AUDIO_MONITORING_HEADPHONES_CONNECTED 0
#define INFO_BTN_NAME               "INFO"
#define Q_BTN_NAME                  "Q/SET"

#define MIN_MSLEEP 11
#define PLAY_MODE (gui_state == GUISTATE_PLAYMENU && CURRENT_GUI_MODE == GUIMODE_PLAY)
#define MENU_MODE (gui_state == GUISTATE_PLAYMENU && CURRENT_GUI_MODE == GUIMODE_MENU)

/* Canon's card format dialogs, for CONFIG_RESTORE_AFTER_FORMAT (src/debug.c).
 * Research: ML-R6\research\features\FORMAT.md. Each word is the dialog
 * pointer, 0 while the dialog is closed:
 * - Begin (the confirmation): set by StartMnCardFormatBeginApp 0xE0BAD43C,
 *   cleared by StopMnCardFormatBeginApp 0xE0BAD0FC or TERMINATE_WINSYS.
 *   The drive it formats is the u16 before it: 1 = A: (slot 1), 2 = B:
 *   (slot 2), as FIO_GetDriveName(wDriveNo - 1) and "%c:" = 'A' + StorageID.
 * - Execute (the progress): set by StartMnCardFormatexcuteApp 0xE0BDEBB8,
 *   before Begin is closed; cleared by StopMnCardFormatexcuteApp 0xE0BDE51E
 *   at progress 100, on an error or on TERMINATE_WINSYS.
 * Begin dialog items: 3 title, 4 "Format cardN", 5 "All data will be
 * lost!", 8 / 9 used / total size (template 0xA1). INFO reaches the dialog
 * as PRESS_INFO and is swallowed there; the low level format is on ERASE.
 * No FORMAT_STR_LOC: no format dialog is left while ML is restored. */
#define DIALOG_MnCardFormatBegin   (0x354CC)
#define DIALOG_MnCardFormatExecute (0x3ACF8)
#define FORMATTING_CF_CARD (*(volatile uint16_t *)0x354C8 == 1) /* formatting A: */
#define FORMAT_BTN       BGMT_INFO
#define FORMAT_BTN_NAME  "[INFO]"

/* Two SD slots, one device struct each: sd_device index of A: / B:
 * (0xE0190FC8: index = StorageID == 1 ? 0 : 1), for src/bootflags.c. */
#define SD_DEVICE_INDEX_A 1
#define SD_DEVICE_INDEX_B 0

/* WRONG: copied straight from 200d/50d via R5 */
// Definitely wrong / hacks / no testing at all:
#define IMGPLAY_ZOOM_LEVEL_ADDR (0x2CBC) //wrong

extern int winsys_bmp_dirty_bit_neg;
#define WINSYS_BMP_DIRTY_BIT_NEG MEM(&winsys_bmp_dirty_bit_neg) // faked via function_overrides.c
#define FOCUS_CONFIRMATION 0 // wrong, but safe, no focus lock

#define LV_BOTTOM_BAR_DISPLAYED 0x0 // wrong, fake bool
// this block all copied from 50D (via R5), and probably wrong, though likely safe
#define FASTEST_SHUTTER_SPEED_RAW 160
#define MAX_AE_EV 2
#define FLASH_MAX_EV 3
#define FLASH_MIN_EV -10
#define COLOR_FG_NONLV 80
#define AF_BTN_HALFSHUTTER 0
#define AF_BTN_STAR 2
// another block copied from 50D (via R5)
#define GUIMODE_WB 5
// 9 is the 50D value, unverified on R6. With BGMT_Q defined, a match would make
// Q on the shooting screen open the ML focus menu (menu.c) - off until known.
#define GUIMODE_FOCUS_MODE -1
#define GUIMODE_DRIVE_MODE 8
#define GUIMODE_PICTURE_STYLE 4
#define GUIMODE_Q_UNAVI 0x18
#define GUIMODE_FLASH_AE 0x22
#define GUIMODE_PICQ 6

// all these MVR ones are junk, don't try and record video.
// MVR_190_STRUCT is the R5 address and NOT valid on R6; movie features stay disabled.
#define MVR_190_STRUCT (*(void**)0x1ed8)
#define div_maybe(a,b) ((a)/(b))
/* Not known on R6. 1, not a made-up 70: since RECORDING works (from
 * PROP_SHOOTING_TYPE), bitrate.c pauses ML's LiveView tasks while
 * MVR_BUFFER_USAGE > buffer.warning.level (30..100), so any fake value at
 * or above the user's level would stall them during every recording.
 * Not 0: an unsigned "0 > level" is a -Wtype-limits error. */
#define MVR_BUFFER_USAGE 1
#define MVR_FRAME_NUMBER (*(int*)(220 + MVR_190_STRUCT))
#define MVR_BYTES_WRITTEN MEM((212 + MVR_190_STRUCT))

// Clip length limit, for FEATURE_OVERRIDE_MOVIE_30_MIN_LIMIT (verified,
// unlike the MVR values above). MVR_TIME_LIMIT_FUNC (0xE074ECC6, no
// arguments) returns the limit in ms: the literal 1799000 (29:59), or 449000
// (7:29) when 0xE07A99EA() == 1 (High Frame Rate). Canon has no other value,
// none for 50p / 59.94p. It is read again each second while recording: the
// DlgLiveView per-second handler (call at 0xE0BD265A) stops the clip when
// the elapsed ms equals it. The only other callers cap the remaining-time
// display at limit / 1000 s (0xE08CCF96, 0xE08CD034). No code branches into
// the function past its entry.
// movtweaks.c replaces it with its own (an 8-byte hook, function_hook_patch),
// which gives each frame rate its limit. Page 0xE0740000: one remapped page.
#define MVR_TIME_LIMIT_FUNC 0xE074ECC6
#define MVR_TIME_LIMIT_FUNC_CODE { 0x10, 0xB5, 0x5A, 0xF0, 0x8F, 0xFE, 0x01, 0x28 } /* push {r4,lr}; bl 0xE07A99EA; cmp r0,#1 */
#define MVR_IS_HIGH_FRAME_RATE() (((int (*)(void)) 0xE07A99EB)() == 1) /* Canon's own test, Thumb */
// Callers that get the real limit, by return address. These are the only
// three bl sites in the ROM, and no data pointer to the function exists:
// - 0xE0BD265E: the clip-stop check (bl at 0xE0BD265A);
// - 0xE08CD038: the remaining time, min(card, limit / 1000) (0xE08CD028),
//   for the shooting-screen time item (0xE060B6CE, shows minutes with
//   MVR_DISPLAY_PATCHES), OlcRecordTime (0xE096CAB6, clamps to 59:59 by
//   itself) and the record-start checks (0xE074D484, 0xE074D53E, 0xE08CD06C);
// - 0xE08CCF9A: the card remaining time (0xE08CCF32) in the state where
//   Canon shows the limit instead (GUI+0x4C == 1); its only live caller
//   is 0xE08CD028 above.
// Any other caller would get at most MVR_TIME_LIMIT_DISPLAY_MAX_SEC (59:59),
// which every minutes:seconds display can show.
#define MVR_TIME_LIMIT_FULL_CALLERS    { 0xE0BD265E, 0xE08CD038, 0xE08CCF9A }
#define MVR_TIME_LIMIT_DISPLAY_MAX_SEC (60 * 60 - 1)

// The MP4 writer limits the clip length too. mp4wInitializeMmlibParam
// (0xE05F07CA, run at each recording start) sizes the per-file sample
// tables for 108000 entries (pool word 0xE05F0BE8; one entry per video
// frame), and MmLibCompose asserts when one is full (Err 70, 0xE03E7120).
// The tables restart only at a new file, and on exFAT the R6 does not
// split at 4 GB. On camera, with the capacity set to 1800, FHD 29.97p
// stopped with Err 70 at about 60 s.
//
// MVR_WRITER_PATCHES (applied by movtweaks.c together with the time limit,
// only when the limit is above Canon's, all or none):
// {address, Canon's value, new value, is an instruction}
// - capacity 108000 -> 216000 entries: 60:03 at 59.94p, 72:00 at 50p,
//   2:00:07 at 29.97p, 2:24:00 at 25p;
// - the table sizes in bytes that go with it: 8-byte tables x2;
//   composition offsets (ctts) 8 x capacity (Canon sized them for 2/3 of
//   the frames, the ratio measured on IPB clips; x2 would leave 0.1 % at
//   60:00, 59.94p);
// - AAC (IPB Light) gets a sample count limit of its own. Canon compares the
//   AAC sample count with the shared capacity (ldr r0,[r5,#0x24];
//   cmp sb,r0 at 0xE03E6EB8): 46.875 samples per second, so 216000 would
//   stop the clip with Err 70 at 76:48. cmp.w sb,#0x54000 instead: 344064
//   samples, 2:02:20. Its table (4 bytes per sample, pool word 0xE05F0BEC)
//   is 4 x 344064 (Canon's 345600 held only 86400 of the 108000 it
//   allowed). LPCM has no such table: its sample count is a sum;
// - the audio chunk table (stsc) 0x8000 -> 0x20000 bytes, 4096 -> 16384
//   entries (lsls r1,r2,#12 -> #14 at 0xE05F08D2). Canon does not check it
//   (its count is compared with the capacity, not with this size): AAC adds
//   about 2.1 per second (measured), so it overflowed silently from
//   32.7 min; now 2:10. LPCM adds one only when the samples per chunk
//   change;
// - the moov buffers (one per file set) 4 -> 8 MB, mov.w fp,#0x400000 at
//   0xE05F02B0. The moov builder has no capacity check; measured on camera
//   about 1176 B/s at 50p with AAC (moov 524260 B for 303 s), so a 60 min
//   moov is about 4.3 MB at 50p and 4.9 MB at 59.94p. The third buffer
//   (moov read back from an existing file, 0xE05F032C) stays 4 MB.
//   Unpatched, a 4..8 MB header would take Canon's path for headers over
//   4 MB (finalize, 0xE05EDAF0), which appends zeros in 1 MB steps; when the
//   free box is shorter than its <= 511 B alignment pad, the header file
//   gets up to 511 extra bytes (about 0.1-0.2 % of such clips, from the
//   ROM). The two 4 MB compares (0xE03A38D6, 0xE05EDAF0) move to 8 MB, so
//   a 4..8 MB header takes the same path as Canon's own headers.
// At 1:59:59, 29.97p: 215,758 frames of the 216,000, AAC 337,453 samples of
// the 344,064, about 15,000 stsc entries; moov about 4.3 MB (LPCM) or 5.9 MB
// (AAC) of the 8 (estimated from the 50p clips).
// Writer pool (MovRecResource, 193.5 MiB) peak on camera: 163.9 MiB with
// the tables doubled, 173 MiB with these but the AAC limit and stsc 16384
// (4K 50p / 59.94p, HFR, FHD); those two add 1.1 MiB (both file sets).
// Tested on camera: a 60 min FHD 50p IPB Light (AAC) clip stopped at
// 59:59 with a 4.46 MB moov and checked fine on a PC (ffprobe, ffmpeg).
// Canon's player refused it, see MVR_PLAYBACK_PATCHES.
// Pages: 0xE05F0000, 0xE03A0000, 0xE03E0000, 0xE05E0000; with the time
// limit, the player, the time display and the heat timer, 8 remapped pages
// (CONFIG_MMU_RUNTIME_PAGES in internals.h).
#define MVR_WRITER_PATCHES { \
    { 0xE05F0BE4,     864000,    1728000, 0 }, /* 8-byte tables x2 */ \
    { 0xE05F0BE8,     108000,     216000, 0 }, /* capacity x2 */ \
    { 0xE03E6EB8, 0x45816A68, 0x2FA8F5B9, 1 }, /* AAC samples: cmp.w sb,#0x54000 */ \
    { 0xE05F0BEC,     345600,    1376256, 0 }, /* AAC table: 4 x 344064 */ \
    { 0xE05F0BF0,     576000,    1728000, 0 }, /* ctts: 8 x capacity */ \
    { 0xE05F08D0, 0x0311200D, 0x0391200D, 1 }, /* audio stsc 4096 -> 16384 */ \
    { 0xE05F02B0, 0x0B80F44F, 0x0B00F44F, 1 }, /* moov buffers 4 -> 8 MB */ \
    { 0xE05F032C, 0xF5B14659, 0xF5B104D1, 1 }, /* 3rd buffer: 8 << 19 = 4 MB */ \
    { 0xE03A38D8, 0xD2030F80, 0xD2030F00, 1 }, /* cmp 4 -> 8 MB */ \
    { 0xE05EDAF0, 0x0F80F5B0, 0x0F00F5B0, 1 }, /* cmp 4 -> 8 MB */ \
}

// Canon's player rejects clips longer than 30 min ("Cannot playback
// movie", no crash): reading a movie's info (mvpGetMovieInfoCBR 0xE04EC1AE
// -> CheckMovieFileInfo 0xE04EBC90 -> ChkTotalFrame 0xE04EB8A2) allows at
// most fps x 1800 s of video frames. The moov itself is read into the
// player's 270 MB region, so its size is not the limit for a single file.
// MVR_PLAYBACK_PATCHES (applied with MVR_WRITER_PATCHES) raise the frame
// count limit, in page 0xE04E0000:
// - 20/24/25/30/50p: cmp.w r1,r3,lsl #3 (r3 = 225 x fps) -> lsl #6, 4 h
//   (lsl #5, 2 h, would leave 25p and 24.00p 20 frames at 1:59:59);
// - 23.976p: movw r0,#0xA894 -> mov.w r0,#0x40000, 3 h;
// - 29.97p: movw r0,#0xD2BA -> mov.w r0,#0x40000, 2.4 h;
// - 59.94p: literal 0x1A574 (loaded only at 0xE04EB91C) -> 0x80000, 2.4 h.
// Not changed: the in-camera editor (cutting) keeps its own 30 min and 4 MB
// limits, so long clips may not be trimmed on the camera.
#define MVR_PLAYBACK_PATCHES { \
    { 0xE04EB8DC, 0x0FC3EBB1, 0x1F83EBB1, 1 }, /* 20..50p: fps x 1800 -> x 14400 */ \
    { 0xE04EB8F4, 0x0094F64A, 0x2080F44F, 1 }, /* 23.976p: 43156 -> 262144 */ \
    { 0xE04EB904, 0x20BAF24D, 0x2080F44F, 1 }, /* 29.97p: 53946 -> 262144 */ \
    { 0xE04EBBB4, 0x0001A574, 0x00080000, 0 }, /* 59.94p: 107892 -> 524288 */ \
}

// The shooting-screen time item (LiveView item 0x3B, 0xE060B6CE) prints
// "%02d:%02d". While recording it shows the elapsed time in total minutes
// (61:00, 119:59). In standby it shows the remaining time with minutes % 60
// (divmod by 60 twice at 0xE060B76A..0xE060B77B), so a 1:29:59 limit read
// 29:59. MVR_DISPLAY_PATCHES (applied with MVR_WRITER_PATCHES) drop the
// second divmod: standby also shows total minutes (89:59, 119:59), the same
// 5 characters. r0 is the quotient of the first divmod; r3 = seconds is set
// next (0xE060B77C) and r1 is not read after. No code branches into
// 0xE060B776..0xE060B77D. Page 0xE0600000.
#define MVR_DISPLAY_PATCHES { \
    { 0xE060B774, 0xF0D6213C, 0xBF004602, 1 }, /* movs r1,#60; blx.. -> mov r2,r0; nop */ \
    { 0xE060B778, 0x460AEC9A, 0xBF00BF00, 1 }, /* ..divmod; mov r2,r1 -> nop; nop */ \
}

// Longest limits, given MVR_WRITER_PATCHES (216000 frames): 2 h (1:59:59)
// up to 30p, 1 h (59:59) at 50p / 59.94p (60:03 at 59.94p).
#define MVR_TIME_LIMIT_MAX_SEC          (2 * 60 * 60)
#define MVR_TIME_LIMIT_MAX_HIGH_FPS_SEC (60 * 60)
// High Frame Rate (119.88p / 100p, no audio) is stored as a slow-motion clip
// at 29.97p / 23.976p / 25p (not checked on a file: Canon's player plays these
// clips, and ChkTotalFrame refuses any other fps code), so Canon's 7:29 is the
// player's 30 min check
// (ChkTotalFrame, MVR_PLAYBACK_PATCHES), not a writer limit: 7:29 at 119.88p
// is 53,826 frames, 29.97p allows 53,946. With the long-clip patches
// (limits above Canon's 29:59), 29:59 fits:
// - writer: 29:59 at 119.88p = 215,664 frames of the 216,000 (ctts too);
// - moov: 18.55 B per frame measured on a 50p clip, so about 4 MB of the 8;
// - player: 262,144 frames at 29.97p / 23.976p (36 min at 119.88p), 360,000
//   at 25p (60 min at 100p).
// Up to Canon's 29:59 nothing is patched and high frame rate keeps 7:29.
#define MVR_TIME_LIMIT_HFR_CANON_SEC (7 * 60 + 29)
#define MVR_TIME_LIMIT_MAX_HFR_SEC (29 * 60 + 59)

// XCM_New 0xE0174FA4 allocates the XCM on the heap, so there is no fixed address.
// XCM_GetContext 0xE0175274 returns xcm + idx*0x7A0 + 0x10; context 0 = XCM + 0x10 (as on R5).
#define XCM_PTR *(unsigned int *)0x14B2D8   // _pXCM
#define XIMR_CONTEXT ((unsigned int *)(XCM_PTR + 0x10))
