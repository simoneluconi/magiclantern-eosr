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
 * GUIMODE_ML_MENU) stops LV, so lv is 0 while a menu is open.
 * force_liveview() has no caller in this build (all callers are behind
 * features or modules that R6 does not enable). */
#define CONFIG_LIVEVIEW

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

/** Cam has the DryOS RPC mechanism between cores (_request_RPC, SGI 0xC) */
#define CONFIG_RPC

/** Cam has MMU (by itself, does nothing, see CONFIG_MMU_REMAP).
 * Canon's live table at 0xDFFC0000 matches the ROM template at 0xE0000000
 * except the two TTBR0 L1 entries at +0x4800 / +0x4880 (checked on camera). */
#define CONFIG_MMU

/** Remapped 64 KiB pages for ROM patches: one reserved at boot, five more
 * taken from Canon's AllocateMemory pool after boot (src/patch_mmu.c).
 * The boot space comes out of DryOS user_mem, only 1.07 MB on R6
 * (0x190098 + 0x112D70, cstart 0xE0100146), which must keep 0x80010 bytes
 * (cstart 0xE0100120; init_task and boot-d678.c check it): with two pages
 * at boot 6.4 KB were left, three did not start. AllocateMemory had
 * 5.4 MB free after boot and while recording 4K (14.5 MB pool).
 * Putting the pages at the start of the AllocMem region before DryOS set
 * it up (0x12C9000) did not start either. */
#define CONFIG_MMU_REMAP_PAGES 1
#define CONFIG_MMU_RUNTIME_PAGES 5

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5

/* src/rom_values.h assumes 64 MiB ROM1 on DIGIC X, R6 has 32 MiB
 * (gang200.bin, mapped at 0xF0000000). ROM0 is 64 MiB, matching the default. */
#define ROM1_SIZE 0x2000000
