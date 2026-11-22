#define FEATURE_VRAM_RGBA

// Like R5: no dedicated layer, we draw into Canon's GUI layer
#define CONFIG_COMPOSITOR_XCM

// Subset of R5.152 features, only read-only diagnostics.
// Left out: FEATURE_DONT_CLICK_ME (calls "dumpf"),
// FEATURE_SHOW_FREE_MEMORY (allocates shoot memory).
#define FEATURE_SHOW_SHUTTER_COUNT
#define FEATURE_SHOW_GUI_EVENTS

// "Internal Temp": read-only prop handler, see EFIC_CELSIUS in consts.h
#define FEATURE_SHOW_CMOS_TEMPERATURE
// Submenu with all of Canon's temperature sensors (PROP_TEMP_* ids)
#define FEATURE_SHOW_TEMP_SENSORS
// Movie > Heat timer: scales the seconds of Canon's time-based heat
// table (HEAT_TABLE_* in consts.h) with patch_mmu. Sensor thresholds
// are not touched.
#define FEATURE_MOVIE_HEAT_TIMER

// CONFIG_TSKMON is on by default
#define FEATURE_SHOW_TASKS
#define FEATURE_SHOW_CPU_USAGE

// Display > Advanced settings > Color scheme, on ML's RGBA palette
// (ML's own graphics only; Canon's are not changed)
#define FEATURE_COLOR_SCHEME

// Display > Red display: LCD / EVF output colour matrix in red, see
// DISP_MATRIX_ADDR in consts.h. Opt-in (OFF by default), saved.
#define FEATURE_DISPLAY_RED_MATRIX

// Enable remapping ROM pages to RAM (patch_mmu), as on M50 / 850D.
// No patches yet: include/platform/mmu_patches.h is empty.
#define CONFIG_SGI_HANDLERS
#define CONFIG_MMU_REMAP

// Movie menu: MOV/MP4 time limit (patches MVR_TIME_LIMIT_* at runtime),
// up to MVR_TIME_LIMIT_MAX_SEC; above Canon's 29:59 the movie writer is
// patched too (MVR_WRITER_PATCHES).
#define FEATURE_OVERRIDE_MOVIE_30_MIN_LIMIT

// ML's LV overlays, with Canon's FaceYuv as the LV image (CONFIG_FACEYUV_LV).
// They draw only while get_global_draw() allows it: in LV, in Canon's
// LV_OVERLAYS_MODE 3 (the clean INFO screen, in photo and movie mode on
// R6). The ones that read the image also need a FaceYuv frame:
// photo mode, LCD or EVF alone (faceyuv.c).
// Only what the R6 lacks; zebras, focus peaking and the histogram are
// Canon's own on R6.
#define FEATURE_GLOBAL_DRAW
#define FEATURE_SPOTMETER

#undef CONFIG_CRASH_LOG
#undef CONFIG_PROP_REQUEST_CHANGE
#undef CONFIG_AUTOBACKUP_ROM
