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

/** The planned ROM patches are in two 64 KiB pages (1 MiB sections 0xE07
 * and 0xE0D), so reserve space for two remapped pages and L2 tables. */
#define CONFIG_MMU_REMAP_PAGES 2

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5

/* src/rom_values.h assumes 64 MiB ROM1 on DIGIC X, R6 has 32 MiB
 * (gang200.bin, mapped at 0xF0000000). ROM0 is 64 MiB, matching the default. */
#define ROM1_SIZE 0x2000000
