#ifndef _patch_mmu_h_
#define _patch_mmu_h_

// Don't include this header file directly, instead include patch.h.
// This header only has the parts required for patching via MMU.

#if !defined(MODULE) && !defined(CONFIG_MMU_REMAP)
    // We want to fail cam builds without CONFIG_MMU_REMAP,
    // but allow module builds (modules can run on both kinds of cams)
    #error "patch_mmu.h included but CONFIG_MMU_REMAP not defined, shouldn't happen"
#endif

#include "patch.h"

struct mmu_L2_page_info
{
    uint8_t *phys_mem[16]; // 16 possible 0x10000 pages of backing ram
    uint8_t *L2_table; // L2 table used for remapping into this page,
                       // size MMU_L2_TABLE_SIZE.
    uint32_t virt_page_mapped; // what 0x100000-aligned VA page addr is mapped here
    uint32_t in_use; // currently used to back a ROM->RAM mapping?
};

struct mmu_config
{
    uint8_t *L1_table; // single region of size MMU_TABLE_SIZE
    uint32_t max_L2_tables;
    struct mmu_L2_page_info *L2_tables; // struct, not direct access to L2 mem, so we can
                                        // easily track which pages are mapped where.
    uint32_t max_64k_pages_remapped;
    uint8_t *phys_mem_pages;
    // CONFIG_MMU_RUNTIME_PAGES: more 64kB pages, taken from Canon's
    // AllocateMemory after boot or on demand (mmu_add_runtime_pages());
    // used once phys_mem_pages are all in use.
    uint32_t runtime_64k_pages;
    uint8_t **runtime_pages;
};

extern struct mmu_config global_mmu_conf; // in patch_mmu.c
extern void change_mmu_tables(uint8_t *ttbr0, uint8_t *ttbr1, uint32_t cpu_id);
void change_mmu_tables_wrapper(void);

int apply_patch(struct patch *patch);
int apply_normal_patches(void);

// Grows the pool of remappable pages by what these patches still need (ROM
// pages not remapped yet, sections without an L2 table yet), for cams with
// CONFIG_MMU_RUNTIME_PAGES_MAX (src/patch_mmu.c). Call before apply_patches();
// task context only; allocates in small blocks; never freed.
int mmu_reserve_pages_for_patches(const struct patch *patches, uint32_t count);

// Remaps the ROM page of addr to RAM with its content unchanged, if not yet
// done (apply_patches calls it for each patch before the batch).
int mmu_remap_rom_page(uint32_t addr);

// Longest interrupts-off section of a ROM patch since boot, in us.
extern uint32_t mmu_patch_max_irqoff_us;

// Sets up structures required for remapping via MMU,
// and applies compile-time specified patches from platform/XXD/include/platform/mmu_patches.h
int mmu_init(void);

#endif // _patch_mmu_h_
