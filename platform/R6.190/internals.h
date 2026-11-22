/**
 * Camera internals for EOS R6 1.9.0
 */

/** This camera has a DIGIC X chip */
#define CONFIG_DIGIC_X

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

#define CONFIG_TASK_STRUCT_V2_SMP
#define CONFIG_TASK_ATTR_STRUCT_V5

/* src/rom_values.h assumes 64 MiB ROM1 on DIGIC X, R6 has 32 MiB
 * (gang200.bin, mapped at 0xF0000000). ROM0 is 64 MiB, matching the default. */
#define ROM1_SIZE 0x2000000
