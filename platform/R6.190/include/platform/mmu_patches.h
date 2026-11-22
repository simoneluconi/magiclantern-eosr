#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

#if CONFIG_FW_VERSION == 190 // ensure our hard-coded patch addresses are not broken
                             // by a FW upgrade

// No patches yet: this only enables patch_mmu so the table copy, the
// cpu0 TTBR switch and boot can be tested on their own.
struct patch early_data_patches[] =
{

};

struct patch normal_data_patches[] =
{

};


struct function_hook_patch early_code_patches[] =
{

};

struct function_hook_patch normal_code_patches[] =
{

};

#endif // R6 FW_VERSION 190

#endif // __PLATFORM_MMU_PATCHES_H__
