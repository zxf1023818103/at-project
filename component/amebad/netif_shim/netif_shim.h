/******************************************************************************
  *
  * This module is a confidential and proprietary property of RealTek and
  * possession or use of this module requires written permission of RealTek.
  *
  * Copyright(c) 2016, Realtek Semiconductor Corporation. All rights reserved.
  *
******************************************************************************/

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NETIF_SHIM_H
#define __NETIF_SHIM_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "platform_stdlib_rtl8721d.h"
#include "platform_opts.h"
#include "autoconf.h"
#include "basic_types.h"        /* u32 / u8 (was pulled via lwip before) */
#include <stddef.h>             /* offsetof for the layout asserts */

/* ---- FreeRTOS+TCP build: lwIP removed -------------------------------------- *
 * This header used to pull lwIP (tcpip.h / lwip/netif.h / lwip/err.h). The IP
 * stack is now FreeRTOS-Plus-TCP; lwIP is gone. What survives is the minimal
 * surface the prebuilt WiFi driver (lib_wlan.a) + wifi_conf.c hard-require:
 *   - struct netif xnetif[] — the driver's per-VIF handle array. This is a FIXED
 *     ABI: the blob accesses hwaddr @ +50 and hwaddr_len @ +56 on a 72-byte
 *     stride (verified against the real lwIP struct as compiled). The asserts
 *     below turn any layout drift into a BUILD error, never silent RAM corruption.
 *   - LwIP_GetMAC() — load-bearing on the connect-path mgmt-frame filter — plus
 *     the other LwIP_Get* / netif_* / dhcp_stop symbols the driver glue still
 *     references (link-resolved by the weak stubs in freertos_tcp_compat.c).
 * Feature paths that needed deeper lwIP (static IP / AP DHCP server / TCP
 * keepalive offload / ARP query) are gated off in wifi_conf.c for this build.   */

/* Pinned STA(0)+AP(1): the blob indexes xnetif[1] — must not collapse to 1. */
#define NET_IF_NUM 2

#ifndef NETIF_MAX_HWADDR_LEN
#define NETIF_MAX_HWADDR_LEN 6
#endif

/* Layout-compatible minimal netif — only the two fields the driver touches are
 * named; everything else is padding to the exact 72-byte array stride. */
struct netif {
	unsigned char _pad0[ 50 ];
	unsigned char hwaddr[ NETIF_MAX_HWADDR_LEN ];   /* +50  (driver reads/writes) */
	unsigned char hwaddr_len;                       /* +56  (driver reads/writes) */
	unsigned char _pad1[ 72 - 57 ];
};
_Static_assert( offsetof( struct netif, hwaddr )     == 50, "xnetif hwaddr offset"     );
_Static_assert( offsetof( struct netif, hwaddr_len ) == 56, "xnetif hwaddr_len offset" );
_Static_assert( sizeof( struct netif )               == 72, "xnetif array stride"      );

/* Private typedef -----------------------------------------------------------*/
typedef enum
{
	DHCP_START=0,
	DHCP_WAIT_ADDRESS,
	DHCP_ADDRESS_ASSIGNED,
	DHCP_RELEASE_IP,
	DHCP_STOP,
	DHCP_TIMEOUT
} DHCP_State_TypeDef;

/* Extern functions ------------------------------------------------------------*/
void wifi_rx_beacon_hdl( char* buf, int buf_len, int flags, void* userdata);
void rtw_wakelock_timeout(u32 timeoutms);

/* Exported functions ------------------------------------------------------- */
void LwIP_Init(void);
uint8_t LwIP_DHCP(uint8_t idx, uint8_t dhcp_state);
void LwIP_ReleaseIP(uint8_t idx);
uint8_t* LwIP_GetMAC(struct netif *pnetif);
uint8_t* LwIP_GetIP(struct netif *pnetif);
uint8_t* LwIP_GetGW(struct netif *pnetif);
uint8_t* LwIP_GetMASK(struct netif *pnetif);
uint8_t* LwIP_GetBC(struct netif *pnetif);
uint32_t LWIP_Get_Dynamic_Sleep_Interval(void);

/* netif link-state + dhcp_stop: referenced by wifi_conf.c's STA bring-up; impls
 * are the weak no-op stubs in app_km4/freertos_tcp_compat.c. */
void netif_set_up(struct netif *netif);
void netif_set_down(struct netif *netif);
void netif_set_link_up(struct netif *netif);
void netif_set_link_down(struct netif *netif);
void dhcp_stop(struct netif *netif);

/* IPv4 address type + helpers, only used by the AP-mode netif_set_addr path in
 * wifi_conf.c (compiled but gc-stripped in STA-only use). lwip v1 ip_addr_t. */
struct ip_addr { u32 addr; };
typedef struct ip_addr ip_addr_t;
#define IP4_ADDR(ipaddr, a, b, c, d) \
	((ipaddr)->addr = ((u32)((d) & 0xff) << 24) | ((u32)((c) & 0xff) << 16) | \
	                  ((u32)((b) & 0xff) << 8)  |  (u32)((a) & 0xff))
void netif_set_addr(struct netif *netif, ip_addr_t *ipaddr, ip_addr_t *netmask, ip_addr_t *gw);

/* Realtek DHCP server (AP mode) — declared here so wifi_conf.c's AP paths compile;
 * gc-stripped in STA-only use (no impl linked). */
void dhcps_init(struct netif *pnetif);
void dhcps_deinit(void);

extern struct netif xnetif[];

#ifdef __cplusplus
}
#endif

#endif /* __NETCONF_H */
