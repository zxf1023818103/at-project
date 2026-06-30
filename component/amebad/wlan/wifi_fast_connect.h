#ifndef _AMEBAD_WIFI_FAST_CONNECT_H_
#define _AMEBAD_WIFI_FAST_CONNECT_H_

/*
 * WiFi fast-connect / fast-reconnect / tickless-roaming API surface.
 *
 * Relocated here (the wlan component) off the SDK example header
 * component/common/example/wlan_fast_connect/example_wlan_fast_connect.h so the
 * function pointers and global variables live with the lib that defines them:
 * the prebuilt WiFi driver lib_wlan.a (freertos_intfs.o) owns
 *   p_wlan_init_done_callback, p_roaming_start_callback, g_reconnect_delay,
 *   psk_essid / psk_passphrase / wpa_global_PSK / psk_passphrase64.
 * p_write_reconnect_ptr is NOT in any lib — the fast-connect / roaming examples
 * assign it; this image provides a weak stub (app_km4/freertos_tcp_compat.c).
 * (The PMC-owned roaming state roaming_type_flag / roaming_sema lives in the pmu
 * component's wifi_roaming.h, since lib_pmc_hp.a defines those.)
 *
 * Consumers: wifi/wifi_conf.c (struct wlan_fast_reconnect + p_write_reconnect_ptr)
 * and at_cmd/atcmd_wifi.h. NET_IF_NUM must be defined before this header (the
 * includers pull netif_shim.h / atcmd_wifi.h first).
 */
#include "FreeRTOS.h"
#include <autoconf.h>
#include "main.h"

#define IW_WPA2_PASSPHRASE_MAX_SIZE 64
#define IW_WPA3_PASSPHRASE_MAX_SIZE 128
#define IW_PASSPHRASE_MAX_SIZE      IW_WPA3_PASSPHRASE_MAX_SIZE
#define NDIS_802_11_LENGTH_SSID     32
#define A_SHA_DIGEST_LEN            20
#define PMK_LEN_MAX                 64

struct wlan_fast_reconnect {
	unsigned char psk_essid[NDIS_802_11_LENGTH_SSID + 4];
	unsigned char psk_passphrase[IW_PASSPHRASE_MAX_SIZE + 1];
	unsigned char wpa_global_PSK[A_SHA_DIGEST_LEN * 2];
	uint32_t	channel;
	uint32_t    security_type;
#if defined(CONFIG_FAST_DHCP) && CONFIG_FAST_DHCP
	uint32_t offer_ip;
#endif
#if ATCMD_VER == ATVER_2
	uint32_t    enable;
#endif
#if defined(CONFIG_ENABLE_WPS) && CONFIG_ENABLE_WPS
	unsigned char is_wps_ap;
#endif
};

typedef int (*wlan_init_done_ptr)(void);
typedef int (*write_reconnect_ptr)(uint8_t *data, uint32_t len);

/* Globals — all defined in lib_wlan.a (freertos_intfs.o). */
extern unsigned char psk_essid[NET_IF_NUM][NDIS_802_11_LENGTH_SSID + 4];
extern unsigned char psk_passphrase[NET_IF_NUM][IW_PASSPHRASE_MAX_SIZE + 1];
extern unsigned char wpa_global_PSK[NET_IF_NUM][PMK_LEN_MAX];
extern unsigned char psk_passphrase64[IW_WPA2_PASSPHRASE_MAX_SIZE + 1];
extern u32 g_reconnect_delay;                          /* lib_wlan.a */

/* Callback hooks. p_wlan_init_done_callback / p_roaming_start_callback are in
 * lib_wlan.a; p_write_reconnect_ptr is the image weak stub (assigned by the
 * fast-connect / roaming examples, which this minimal image does not build). */
extern wlan_init_done_ptr  p_wlan_init_done_callback;  /* lib_wlan.a */
extern write_reconnect_ptr p_roaming_start_callback;   /* lib_wlan.a */
extern write_reconnect_ptr p_write_reconnect_ptr;      /* weak stub (image) */

#endif /* _AMEBAD_WIFI_FAST_CONNECT_H_ */
