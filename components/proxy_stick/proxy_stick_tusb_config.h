#pragma once

// Extend Espressif's configuration, preserving every descriptor/endpoint/buffer setting.
#include "tusb_config.h"

#ifdef __cplusplus
extern "C" {
#endif
int proxy_stick_tusb_printf(const char *format, ...);
#ifdef __cplusplus
}
#endif

#undef CFG_TUSB_DEBUG_PRINTF
#define CFG_TUSB_DEBUG_PRINTF proxy_stick_tusb_printf
// Level 3 dumps every payload; level 2 logs protocol decisions without copying image contents.
#undef CFG_TUD_LOG_LEVEL
#define CFG_TUD_LOG_LEVEL 2
#undef CFG_TUD_MSC_LOG_LEVEL
#define CFG_TUD_MSC_LOG_LEVEL 2
