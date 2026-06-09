#ifndef _AMEBAD_WIFI_CONN_PRI_H_
#define _AMEBAD_WIFI_CONN_PRI_H_

/*
 * Connection-priority condition callback owned by the prebuilt WiFi driver
 * lib_wlan.a.
 *
 * Relocated here (the wlan component) off the SDK example
 * component/common/example/wifi_connection_priority/example_wifi_conn_pri_cond.*
 * so the driver-owned global lives with the lib that defines it:
 *   pr_conn_pri_cond_cb   lib_wlan.a (B; also referenced U inside the lib) —
 *                         user hook to override the default strategy when
 *                         choosing between same-SSID/same-security APs. If
 *                         left NULL the driver uses its default strategy; the
 *                         example assigns it (pr_conn_pri_cond_cb = cb).
 *
 * No consumer in the minimal driver-only image yet (the conn-priority example
 * is not built); this is the declaration home for when it is wired.
 *
 * WLAN_BSSID_EX comes from the sibling wlan_bssdef.h; bool from platform_stdlib_rtl8721d.h.
 */
#include "platform_stdlib_rtl8721d.h"
#include "wlan_bssdef.h"

typedef bool (*rtw_conn_pri_cond_cb_t)(WLAN_BSSID_EX *old_ap, WLAN_BSSID_EX *new_ap);

extern rtw_conn_pri_cond_cb_t pr_conn_pri_cond_cb;   /* lib_wlan.a */

#endif /* _AMEBAD_WIFI_CONN_PRI_H_ */
