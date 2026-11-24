#ifndef _property_whitelist_h_
#define _property_whitelist_h_

#include "property.h"

// By default, we allow "reads" (property_handler, ML hooking) for any property.
//
// Initial support for Digic 6, 7, 8 cams found some prop handlers triggered
// bad behaviour (crashes, hangs).  These are blacklisted for both read and write
// until investigated and fixed.
//
// For Digic 4 and 5 cams, "writes" (prop_request_change()) are controlled
// by a global flag, CONFIG_PROP_REQUEST_CHANGE.  This is all or nothing.
//
// For D678, we disallow all "writes" via the same flag, so,
// CONFIG_PROP_REQUEST_CHANGE should be #undef for new ports in features.h.
// Writing to props can brick cams if mistakes are made.
//
// Additionally, even when defined, we now have a per property check (on D678),
// so if prop_write_allow[] is empty, no writes will be attempted even
// with CONFIG_PROP_REQUEST_CHANGE.  This allows devs to enable properties
// one at a time after checking correctness via reversing, tests, etc.

// DO NOT put the same property in both lists;
// that would allow writes but not reads, which is untested
// (and unnecessary as far as I know).

// deny reads / do not register property handlers for these
const uint32_t prop_handler_deny[] =
{
    PROP_ISO,
    PROP_MVR_REC_START, // probably related to MVR stubs being all wrong
    PROP_LV_AFFRAME // so far crash only confirmed on Digic 8
};

// allow writes / allow prop_request_change() for these:
const uint32_t prop_write_allow[] =
{
    // CONFIG_BATTERY_INFO's poll only (src/property.c, battery.c): len 1,
    // value 0x1F, byte for byte Canon's own request 0xE07AE768 (Battery
    // info menu, battery warning); the MPU answers with a new report.
    // CONFIG_PROP_REQUEST_CHANGE stays off: nothing else is written.
    PROP_BATTERY_REPORT,
    // CONFIG_RESTORE_AFTER_FORMAT's restart only (src/property.c): value 0,
    // len 4, as Canon's firmware update "complete" dialogs request it
    // (0xE07AE806); handled by the CameraConductor (0xE0348A13).
    PROP_REBOOT,
};

// anything not listed above will allow reads but not writes

#endif
