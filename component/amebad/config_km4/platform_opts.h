/*
 * at-project minimal AmebaD KM4 config override.
 *
 * The SDK example's inc_hp/platform_opts.h turns WiFi + BT on
 * (CONFIG_WLAN / CONFIG_BT = 1, unguarded), which would drag the full
 * wifi/bt AT command stack (atcmd_wifi.c, atcmd_bt.c, lwip, ...) into
 * log_service.c. For the minimal "basic AT log" firmware we chain to the
 * SDK's config via #include_next and then force the heavy subsystems off.
 *
 * Scope: this directory is placed FIRST on the include path of the
 * at_cmd_km4 component ONLY (not globally). log_service.c's use of
 * CONFIG_WLAN/CONFIG_BT is limited to *which command tables it registers*
 * — a local decision, no shared-struct ABI — so a component-local override
 * is safe and avoids recompiling the whole KM4 tree.
 */
#ifndef AT_PROJECT_KM4_PLATFORM_OPTS_OVERRIDE
#define AT_PROJECT_KM4_PLATFORM_OPTS_OVERRIDE

#include_next "platform_opts.h"

#undef  CONFIG_WLAN
#define CONFIG_WLAN   0
#undef  CONFIG_BT
#define CONFIG_BT     0
#undef  CONFIG_BT_EN
#define CONFIG_BT_EN  0

#endif /* AT_PROJECT_KM4_PLATFORM_OPTS_OVERRIDE */
