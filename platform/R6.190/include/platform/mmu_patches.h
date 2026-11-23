#ifndef __PLATFORM_MMU_PATCHES_H__
#define __PLATFORM_MMU_PATCHES_H__

#include "patch.h"

#if CONFIG_FW_VERSION == 190 // ensure our hard-coded patch addresses are not broken
                             // by a FW upgrade

struct patch early_data_patches[] =
{

};

struct patch normal_data_patches[] =
{

};

/* Dials under the ML menu while recording.
 *
 * Dial switch events reach CameraConductor (CC) before the GUI. For each
 * dial, CC's LV and MOVIE state tables (0xE05E3CF2.., 0xE02FA3EE..) call a
 * judge, 0xE02EBBA6(sw_id, &func), and act on its answer:
 * 0 drop, 1 not handled, 2 CC changes the assigned setting itself
 * (Tv / Av / ISO / exposure comp, 0xE06E118C), 3 forward to the GUI only
 * (0xE0287C50 -> GUI event, which the ML menu eats).
 * The judge returns 2 or 3 from CC's GUI state (ctx+0x940). With Canon's
 * MENU under the ML menu that state gives 3. While recording Canon does not
 * open its MENU (GUIMODE_ML_MENU is 0 then), so CC answered 2 and the dials
 * changed exposure under the ML menu (seen on camera).
 *
 * While the ML menu is shown, the hook answers 3 for the main dial (0x35),
 * the two rear dials (0x36, 0x37) and the lens control ring (0x64): the
 * dials only move the ML menu. Otherwise it runs the original code.
 * sw ids: BL callers of 0xE02EBBA6 in both state tables. Callers that get
 * 3 don't read func. */
static int __attribute__((used)) cc_dial_to_gui_only(int sw_id)
{
    extern int gui_menu_shown(void);
    if (!gui_menu_shown())
        return 0;
    return sw_id == 0x35 || sw_id == 0x36 || sw_id == 0x37 || sw_id == 0x64;
}

void __attribute__((noreturn,noinline,naked,aligned(4))) hook_cc_dial_judge(void)
{
    asm(
        // r0 = sw_id, r1 = &func; keep both (and r2, r3) for the original
        "push   {r0-r3, r12, lr}\n"
        "bl     cc_dial_to_gui_only\n"
        "cmp    r0, #0\n"
        "pop    {r0-r3, r12, lr}\n"
        "beq    1f\n"
        "movs   r0, #3\n"
        "bx     lr\n"

        // overwritten instructions; movs sets Z for the beq at 0xE02EBBAE
        "1:\n"
        "push.w {r4-r10, lr}\n"
        "movs   r4, r1\n"
        "mov    r6, r0\n"
        // ldr pc keeps the flags; bit 0 set: stay in Thumb
        "ldr    pc, =0xE02EBBAF\n"
        ".ltorg\n"
    );
}

struct function_hook_patch early_code_patches[] =
{

};

struct function_hook_patch normal_code_patches[] =
{
    {
        .patch_addr = 0xE02EBBA6, // CC dial judge (sw_id, &func)
        .orig_content = {0x2d, 0xe9, 0xf0, 0x47, 0x0c, 0x00, 0x06, 0x46}, // push.w {r4-r10,lr}; movs r4,r1; mov r6,r0
        .target_function_addr = (uint32_t)hook_cc_dial_judge,
        .description = "Dials: ML menu only while it is open"
    },
};

#endif // R6 FW_VERSION 190

#endif // __PLATFORM_MMU_PATCHES_H__
