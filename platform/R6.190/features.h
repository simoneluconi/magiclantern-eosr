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

// CONFIG_TSKMON is on by default
#define FEATURE_SHOW_TASKS
#define FEATURE_SHOW_CPU_USAGE

// Enable remapping ROM pages to RAM (patch_mmu), as on M50 / 850D.
// No patches yet: include/platform/mmu_patches.h is empty.
#define CONFIG_SGI_HANDLERS
#define CONFIG_MMU_REMAP

#undef CONFIG_CRASH_LOG
#undef CONFIG_PROP_REQUEST_CHANGE
#undef CONFIG_AUTOBACKUP_ROM
