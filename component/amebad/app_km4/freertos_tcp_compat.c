/*
 * Weak stubs for the lwip IP-stack symbols that wifi_conf.c / the Realtek glue
 * (lwip_intf.c) still reference, now that the lwip IP stack is NOT linked
 * (FreeRTOS-Plus-TCP takes its place for this build).
 *
 * These sit on wifi_conf.c's connect-time lwip-DHCP / status-report paths and on
 * lwip_intf.c's netif link-state calls, none of which drive our DHCP (FreeRTOS+TCP
 * does). All weak so linking the real lwip later overrides them. Args are taken
 * as void* and ignored — only the symbol needs to resolve.
 */

/* lwip netif link-state — lwip_intf.c calls these when the driver signals
 * link up/down. FreeRTOS+TCP tracks link state itself (prvGetPhyLinkStatus). */
__attribute__((weak)) void netif_set_up( void * n )        { ( void ) n; }
__attribute__((weak)) void netif_set_down( void * n )      { ( void ) n; }
__attribute__((weak)) void netif_set_link_up( void * n )   { ( void ) n; }
__attribute__((weak)) void netif_set_link_down( void * n ) { ( void ) n; }

/* lwip DHCP stop (status path, not ours). LwIP_GetMAC/IP/GW/MASK are provided
 * for-real in component/amebad/freertos_tcp/realtek_xnetif.c — they must return
 * valid pointers (the mgmt-frame handler dereferences LwIP_GetMAC's result). */
__attribute__((weak)) void dhcp_stop( void * n )           { ( void ) n; }

/* lwip core helper used by lwip_intf.c's netif_is_valid_IP. */
__attribute__((weak)) int ip4_addr_isbroadcast_u32( unsigned int addr, void * netif )
{
    ( void ) addr; ( void ) netif; return 0;
}

/* power-mgmt / fast-reconnect hooks (unused on the basic connect path). */
__attribute__((weak)) void lwip_POST_SLEEP_PROCESSING( void ) { }
__attribute__((weak)) void * p_write_reconnect_ptr = 0;

/* promiscuous mode (connect path references; never enabled here). */
__attribute__((weak)) int  is_promisc_enabled( void )      { return 0; }
__attribute__((weak)) int  promisc_set( int enable, void * cb, unsigned int len )
{
    ( void ) enable; ( void ) cb; ( void ) len; return 0;
}
