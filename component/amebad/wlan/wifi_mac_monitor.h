#ifndef _AMEBAD_WIFI_MAC_MONITOR_H_
#define _AMEBAD_WIFI_MAC_MONITOR_H_

/*
 * MAC-monitor callback hook owned by the prebuilt WiFi driver lib_wlan.a.
 *
 * Relocated here (the wlan component) off the SDK example
 * component/common/example/wifi_mac_monitor/example_wifi_mac_monitor.* so the
 * driver-owned global lives with the lib that defines it:
 *   mac_monitor_callback   lib_wlan.a (B) — invoked by the driver with the
 *                          monitored station's MAC + measured RSSI; the
 *                          example assigns it (mac_monitor_callback = cb) to
 *                          receive per-frame MAC/RSSI notifications.
 *
 * No consumer in the minimal driver-only image yet (the mac-monitor example is
 * not built); this is the declaration home for when it is wired.
 */

typedef int (*mac_monitor_ptr)(unsigned char *phone_macaddr, signed char real_rssi);

extern mac_monitor_ptr mac_monitor_callback;   /* lib_wlan.a */

#endif /* _AMEBAD_WIFI_MAC_MONITOR_H_ */
