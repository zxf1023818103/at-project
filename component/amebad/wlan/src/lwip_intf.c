/******************************************************************************
  *
  * This module is a confidential and proprietary property of RealTek and
  * possession or use of this module requires written permission of RealTek.
  *
  * Copyright(c) 2016, Realtek Semiconductor Corporation. All rights reserved.
  *
******************************************************************************/

/*
 * FreeRTOS-Plus-TCP rewrite of the Realtek WiFi-driver <-> IP-stack seam.
 *
 * Upstream this file bridged the driver to lwIP (CONFIG_LWIP_LAYER): it delivered
 * RX through lwIP's ethernetif_recv(&xnetif[idx]), validated destination IPs
 * against xnetif's lwIP ip_addr, and read IP/GW/MASK out of xnetif for WOWLAN.
 * This build runs FreeRTOS-Plus-TCP instead, so those lwIP-specific paths are
 * replaced. The entry points the prebuilt lib_wlan.a calls keep their (lwIP-free)
 * signatures — rltk_wlan_set_netif_info / rltk_wlan_send / rltk_wlan_recv /
 * netif_rx / netif_is_valid_IP / netif_get_hwaddr / netif_*_sleep_processing — but
 * the stack work is delegated to the FreeRTOS+TCP NetworkInterface
 * (component/amebad/freertos_tcp_port/realtek_netif.c) via the amebad_tcp_* helpers
 * below. No lwIP types are referenced here. The TX/RX skb copy is driver-only and
 * now compiles unconditionally (it never depended on the IP stack).
 *
 * One lwIP-typed shim remains *outside* this file: the prebuilt driver and the
 * vendored wifi_conf.c still reference `xnetif[]` + LwIP_GetMAC for the connect-
 * time management-frame filter — that minimal array lives in realtek_xnetif.c.
 */
#include <autoconf.h>
#include <lwip_intf.h>
#include <osdep_service.h>
#include <wifi/wifi_util.h>

#ifdef WIFI_PERFORMANCE_MONITOR
#include <wifi_performance_monitor.h>
#else
#define WIFI_MONITOR_TIMER_START(x)
#define WIFI_MONITOR_TIMER_END(x, len)
#endif

#ifndef NET_IF_NUM
#define NET_IF_NUM 2
#endif

/* ---- FreeRTOS+TCP NetworkInterface glue (realtek_netif.c) ------------------ */
extern void            amebad_tcp_netif_input( int idx, unsigned int len );        /* RX -> IP task */
extern int             amebad_tcp_is_valid_ip( int idx, const unsigned char * ip_dest );
extern unsigned char * amebad_tcp_get_addr( int idx, int kind );                   /* 0=IP 1=GW 2=MASK */

/* Per-VIF MAC, set by the driver via rltk_wlan_set_netif_info(), returned by
 * netif_get_hwaddr(). (Replaces stashing the MAC in lwIP's xnetif[].hwaddr.) */
static unsigned char wlan_hwaddr[ NET_IF_NUM ][ 6 ];

/**
 *      rltk_wlan_set_netif_info - register a VIF's MAC with this glue.
 *      @idx_wlan: netif index (0 STA / 1 SoftAP in concurrent)
 *      @dev: reserved (was the lwIP netdev pointer) — unused
 *      @dev_addr: interface MAC
 */
void rltk_wlan_set_netif_info(int idx_wlan, void * dev, unsigned char * dev_addr)
{
	( void ) dev;
	if( ( idx_wlan >= 0 ) && ( idx_wlan < NET_IF_NUM ) ) {
		rtw_memcpy( wlan_hwaddr[ idx_wlan ], dev_addr, 6 );
	}
}

/**
 *      rltk_wlan_send - send IP packets to WLAN. Called by the stack's output
 *      path (FreeRTOS+TCP NetworkInterfaceOutput -> here). Driver-skb only —
 *      independent of which IP stack is on top.
 */
int rltk_wlan_send(int idx, struct eth_drv_sg *sg_list, int sg_len, int total_len)
{
	struct eth_drv_sg *last_sg;
	struct sk_buff *skb = NULL;
	int ret = 0;

	WIFI_MONITOR_TIMER_START(wifi_time_test.wlan_send_time);
	if(idx == -1){
		DBG_ERR("netif is DOWN");
		return -1;
	}
	DBG_TRACE("%s is called", __FUNCTION__);

	save_and_cli();
	if (rltk_wlan_check_isup(idx)) {
		rltk_wlan_tx_inc(idx);
	} else {
		DBG_ERR("netif is DOWN");
		restore_flags();
		return -1;
	}
	restore_flags();

	WIFI_MONITOR_TIMER_START(wifi_time_test.wlan_send_time1);
	skb = rltk_wlan_alloc_skb(total_len);
	WIFI_MONITOR_TIMER_END(wifi_time_test.wlan_send_time1, total_len);
	if (skb == NULL) {
		//DBG_ERR("rltk_wlan_alloc_skb() for data len=%d failed!", total_len);
		ret = -1;
		goto exit;
	}
	WIFI_MONITOR_TIMER_START(wifi_time_test.wlan_send_time2);
	for (last_sg = &sg_list[sg_len]; sg_list < last_sg; ++sg_list) {
		rtw_memcpy(skb->tail, (void *)(sg_list->buf), sg_list->len);
		skb_put(skb,  sg_list->len);
	}
	WIFI_MONITOR_TIMER_END(wifi_time_test.wlan_send_time2, total_len);

	WIFI_MONITOR_TIMER_START(wifi_time_test.wlan_send_skb_time);
	rltk_wlan_send_skb(idx, skb);
	WIFI_MONITOR_TIMER_END(wifi_time_test.wlan_send_skb_time, total_len);
	WIFI_MONITOR_TIMER_END(wifi_time_test.wlan_send_time, total_len);

exit:
	save_and_cli();
	rltk_wlan_tx_dec(idx);
	restore_flags();
	return ret;
}

/**
 *      rltk_wlan_recv - copy a pending RX frame out of the driver into the caller's
 *      scatter-gather buffer. Called by amebad_tcp_netif_input() (the RX bridge).
 *      Driver-skb only — stack-independent.
 */
void rltk_wlan_recv(int idx, struct eth_drv_sg *sg_list, int sg_len)
{
	struct eth_drv_sg *last_sg;
	struct sk_buff *skb;

	DBG_TRACE("%s is called", __FUNCTION__);
	if(idx == -1){
		DBG_ERR("skb is NULL");
		return;
	}
	skb = rltk_wlan_get_recv_skb(idx);
	DBG_ASSERT(skb, "No pending rx skb");

	for (last_sg = &sg_list[sg_len]; sg_list < last_sg; ++sg_list) {
		if (sg_list->buf != 0) {
			rtw_memcpy((void *)(sg_list->buf), skb->data, sg_list->len);
			skb_pull(skb, sg_list->len);
		}
	}
}

/*
 *      amebad_recv_skb_len - length of the driver's pending RX skb for idx.
 *      TizenRT's netif_rx uses skb->len (the authoritative frame length), NOT the
 *      `len` argument the driver passes to netif_rx(); the FreeRTOS+TCP RX bridge
 *      uses this to size/copy the exact skb instead of trusting `len`. Returns
 *      0xFFFFFFFF if there's no pending skb (caller falls back to `len`).
 */
unsigned int amebad_recv_skb_len(int idx)
{
	struct sk_buff *skb;
	if (idx == -1) {
		return 0xFFFFFFFFu;
	}
	skb = rltk_wlan_get_recv_skb(idx);
	return (skb != NULL) ? (unsigned int)skb->len : 0xFFFFFFFFu;
}

/*
 *      netif_is_valid_IP - driver RX filter: is this destination IP for us?
 *      Delegated to the FreeRTOS+TCP endpoint (accept-on-uncertainty is safe).
 */
int netif_is_valid_IP(int idx, unsigned char *ip_dest)
{
#if defined(CONFIG_MBED_ENABLED)
	return 1;
#else
	return amebad_tcp_is_valid_ip(idx, ip_dest);
#endif
}

#if !defined(CONFIG_MBED_ENABLED)
unsigned char *netif_get_hwaddr(int idx_wlan)
{
	if( ( idx_wlan >= 0 ) && ( idx_wlan < NET_IF_NUM ) ) {
		return wlan_hwaddr[ idx_wlan ];
	}
	return NULL;
}
#endif

#if defined(CONFIG_MBED_ENABLED)
emac_callback emac_callback_func = NULL;
void *emac_callback_data = NULL;
void set_callback_func(emac_callback p, void *data)
{
	emac_callback_func = p;
	emac_callback_data = data;
}
#endif

/*
 *      netif_rx - the driver hands an RX frame up (task context). Route it to the
 *      FreeRTOS+TCP RX bridge instead of lwIP's ethernetif_recv(&xnetif[idx]).
 */
void netif_rx(int idx, unsigned int len)
{
	WIFI_MONITOR_TIMER_START(wifi_time_test.netif_rx_time);
#if defined(CONFIG_MBED_ENABLED)
	emac_callback_func(emac_callback_data, NULL, len);
#else
	amebad_tcp_netif_input(idx, len);
#endif
#if (CONFIG_INIC_EN == 1)
        inic_netif_rx(idx, len);
#endif
	WIFI_MONITOR_TIMER_END(wifi_time_test.netif_rx_time, len);
}

/* FreeRTOS+TCP has no lwIP ARP-timer to re-arm on sleep wake; these were the lwIP
 * tickless hooks (lwip_PRE/POST_SLEEP_PROCESSING). No-ops here — power-save /
 * tickless idle is off in this driver-only build. */
void netif_post_sleep_processing(void)
{
}

void netif_pre_sleep_processing(void)
{
}

#ifdef CONFIG_WOWLAN
int rtw_default_netif_idx = 0;
void rltk_wlan_set_default_idx(int idx){
	rtw_default_netif_idx = idx;
}

unsigned char *rltk_wlan_get_ip(int idx){
	return amebad_tcp_get_addr(rtw_default_netif_idx ? rtw_default_netif_idx : idx, 0);
}
unsigned char *rltk_wlan_get_gw(int idx){
	return amebad_tcp_get_addr(rtw_default_netif_idx ? rtw_default_netif_idx : idx, 1);
}
unsigned char *rltk_wlan_get_gwmask(int idx){
	return amebad_tcp_get_addr(rtw_default_netif_idx ? rtw_default_netif_idx : idx, 2);
}
#endif
