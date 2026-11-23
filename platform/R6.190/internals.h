/**
 * Camera internals for EOS R6 1.9.0
 */

/** This camera has a DIGIC X chip */
#define CONFIG_DIGIC_X

/** This camera has a full-frame sensor */
#define CONFIG_FULLFRAME

/** Digic 6 and up does not have bitmap font in ROM, try to load it from card **/
#define CONFIG_NO_BFNT

/** disable SRM for now
 * DIGIC X has no old SRM / RscMgr; SRM_*1stJob are dummies that assert
 * (0xE080BB28 / 0xE080BB32, "Resource::RscMgrOldApiStub.c").
 */
#define CONFIG_MEMORY_SRM_NOT_WORKING

/** shoot_malloc (AllocateMemoryResource / AllocateContinuousMemoryResource)
 * gives Err 70 on R6: in PC connection mode (PTP downloads, see
 * src/ptp-chdk.c) and from photo LiveView (a 1 MB screenshot buffer).
 * ML's large buffers come from AllocateMemory instead (14.5 MB pool,
 * about 5 MB free after boot). */
#define CONFIG_MEMORY_SHOOT_MALLOC_NOT_WORKING

/* malloc struct at MALLOC_STRUCT_ADDR uses the V2 layout
 * (get_malloc_info 0xE037C234 reads +4, +8, +0x18, +0x1C, +0x20) */
#define CONFIG_MALLOC_STRUCT_V2

/** ML's lv flag follows PROP_LV_ACTION (src/propvalues.c, lv = !value).
 * On R6 0x80050022 is 0 while LV runs, 1 when it is stopped:
 * - DispOperator's property handler compares 0x80050022 at 0xE0750DCA
 *   ("PROP_LV_ACTION = %d", 0xE0751124)
 * - GuiMainEventHandler handle_START_LV_XX calls GUI_SetLvAction(0)
 *   (0xE07AA730) at 0xE0BD3830
 * - GmtFactory lv_start requests the property with value 0, length 4
 *   (0xE011DC16)
 * Per andrewpoison's camera notes, Canon's MENU (also under the ML menu,
 * GUIMODE_ML_MENU) stops LV, so lv is 0 while a menu is open. */
#define CONFIG_LIVEVIEW

/** RECORDING / RECORDING_H264 follow PROP_SHOOTING_TYPE == 4, as on 50D
 * (src/propvalues.c). PROP_MVR_REC_START stays on prop_handler_deny.
 * DlgLiveView sets it, as GUIGate_NotifyShootType (0xE04DE670) maps its
 * GUI type 2 to a 2-byte request {4, 0}:
 * - handleMovieRecordSecCountEvent "StartEncode" (0xE0BD260A), when the
 *   GUI recording state (GUI_SetRecordingState 0xE079B0B0) goes 2 -> 3
 * - the error / posture callback that also sets state 3 (0xE0BD2BA8)
 * The stop callback (0xE0BD274E) sends GUI type 1, i.e. 3 = LV.
 * So RECORDING is 0 during Canon's first second (states 1 and 2) and
 * while the file is closed (state 5). Canon's heat model checks the same
 * value (0xE04E8440). */
#define CONFIG_RECORDING_FROM_SHOOTING_TYPE

/** Does the mode *dial* have movie mode? R6 does not: photo / movie is a
 * separate switch, as on 200D / 6D2. In movie mode PROP_SHOOTING_MODE_2
 * keeps the dial's mode (e.g. 3 = M), never SHOOTMODE_MOVIE, and
 * PROP_LV_MOVIE_SELECT is 2 (LVMS_ENABLE_MOVIE); andrewpoison saw the
 * same on camera. Without this, is_movie_mode() was always 0: the
 * LiveView info bars showed photo items while recording. */
#define CONFIG_NO_DEDICATED_MOVIE_MODE

/** Canon's "REC" indicator is drawn at the top right of the screen, in
 * ML's LiveView top bar (seen on camera, movie mode): keep that place
 * free while recording (src/lens.c, free_space_update). */
#define CONFIG_REC_INDICATOR_IN_TOP_BAR

/** PROP_BATTERY_POWER holds Canon's CCAPI battery levels, not 0/1/2 bars:
 * 0 low, 1 half, 2 full, 3 none, 4 high, 5 quarter, 6 error, 7 invalid,
 * 8..10 charging (names: CCAPI enum table at 0xE1C914AC; icon order from
 * the GUI mapping at 0xE0A616F6). src/lens.c, batt_update. */
#define CONFIG_BATTERY_POWER_CCAPI_LEVELS

/** After a shooting mode change (photo <-> movie, mode dial), Canon shows
 * its info for 2..3 s even on the clean INFO screen (seen on camera):
 * no ML overlays meanwhile (src/zebra.c, get_global_draw). */
#define CONFIG_LV_INFO_AFTER_MODE_CHANGE_MS 3000

/** force_liveview() does nothing (src/movtweaks.c). It would call
 * SetGUIRequestMode(0) and fake BGMT_LV, which is a dummy on R6 (gui.h,
 * "MILC, always LV"); andrewpoison's port avoided SetGUIRequestMode.
 * Its only caller in this build is motion detect, which calls it when lv
 * is 0 (e.g. display off with LV stopped); it then simply waits for LV. */
#define CONFIG_NO_FORCE_LIVEVIEW

/** ML's LV image (get_yuv422_vram) is Canon's FaceYuv buffer, see
 * faceyuv.c: FACEYUV_WIDTH x FACEYUV_HEIGHT UYVY with signed chroma,
 * shown on ML's whole 720x480 area (src/vram.c, src/imgconv.h). */
#define CONFIG_FACEYUV_LV

/** PTP (only used when building with CONFIG_PTP=y):
 * send_data() is asynchronous, (handle, buf, size, total, callback, cb_priv, 0).
 * Canon's GetDeviceInfo (0xE02B4780) passes callback 0xE02B4761, which stores
 * the status at priv[1] and gives the semaphore at priv[0], then waits on it. */
#define CONFIG_PTP_ASYNC_SEND

/** recv_data() takes a whole data phase in one call,
 * recv_data(handle, buf, get_data_size(), 0, 0) (SetDevicePropValueEx
 * 0xE02B7B54, SendObject 0xE02B6170). Split reads crashed the camera (ERR70). */
#define CONFIG_PTP_RECV_ONCE

/** PROP_SHUTTER / PROP_APERTURE (and their _AUTO variants, if sent) are
 * 2 bytes, signed APEX * 768 ("Code768", see src/lens.h) instead of ML's
 * raw codes; seen on camera: f/1.4 = 0x0300, 1/125 = 0x1500, 1 s = 0,
 * 4 s = 0xFA00. The lens data uses the same unit: Canon converts the
 * lens' raw apertures with (raw - 8) * 96 (0xE05702F4).
 * The R5 may well be the same; not checked. */
#define CONFIG_EXPO_CODE768

/** SW1() / SW2() call Canon's shutter switch callbacks CC_SW1_CBR /
 * CC_SW2_CBR (stubs.S) instead of writing PROP_REMOTE_SW1 / SW2:
 * CONFIG_PROP_REQUEST_CHANGE is off and prop_write_allow[] is empty, so the
 * property path does nothing here. The callbacks are what Canon's MPU
 * interrupt calls for the real button; andrewpoison took pictures with them
 * (227138ec7). Guards and RAM words: src/lens.c, consts.h (CC_*). */
#define CONFIG_SW12_VIA_CC

/** Cam has the DryOS RPC mechanism between cores (_request_RPC, SGI 0xC) */
#define CONFIG_RPC

/** Cam has MMU (by itself, does nothing, see CONFIG_MMU_REMAP).
 * Canon's live table at 0xDFFC0000 matches the ROM template at 0xE0000000
 * except the two TTBR0 L1 entries at +0x4800 / +0x4880 (checked on camera). */
#define CONFIG_MMU

/** Remapped 64 KiB pages for ROM patches: one reserved at boot, six more
 * taken from Canon's AllocateMemory pool after boot (src/patch_mmu.c).
 * In use: time limit 0xE074, movie writer 0xE05F / 0xE03A / 0xE05E, player
 * 0xE04E, heat timer 0xE0D9, dial hook 0xE02E (mmu_patches.h, applied
 * first, so it takes the boot page); none spare.
 * The boot space comes out of DryOS user_mem, only 1.07 MB on R6
 * (0x190098 + 0x112D70, cstart 0xE0100146), which must keep 0x80010 bytes
 * (cstart 0xE0100120; init_task and boot-d678.c check it): with two pages
 * at boot 6.4 KB were left, three did not start. AllocateMemory had
 * 5.4 MB free after boot and while recording 4K (14.5 MB pool).
 * Putting the pages at the start of the AllocMem region before DryOS set
 * it up (0x12C9000) did not start either. */
#define CONFIG_MMU_REMAP_PAGES 1
#define CONFIG_MMU_RUNTIME_PAGES 6

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5

/* src/rom_values.h assumes 64 MiB ROM1 on DIGIC X, R6 has 32 MiB
 * (gang200.bin, mapped at 0xF0000000). ROM0 is 64 MiB, matching the default. */
#define ROM1_SIZE 0x2000000
