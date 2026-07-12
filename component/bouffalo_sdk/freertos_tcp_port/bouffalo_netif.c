/*
 * FreeRTOS-Plus-TCP <-> Bouffalo fhost (wifi6) driver bridge (BL616 / BL616CL).
 *
 * Replaces the wl80211 bridge. The shared NetworkInterface boilerplate lives in
 * component/freertos-plus-tcp_config/at_netif.c; this file implements fhost's net_al.h
 * interface (the ~23 symbols libfhost_bl616_default.a references as undefined) against
 * FreeRTOS+TCP instead of the SDK's lwip adapter (wifi6_lwip_adapter/net_al.c).
 *
 * Data path (fhost drives RX/TX via net_al_*; we bridge to at_netif):
 *   TX  IP task -> at_netif prvOutput -> ops.tx=prvTx -> net_buf_tx_alloc_fill + net_al_tx_req
 *                 -> fhost_tx_req_do (fhost core queues to the MAC)
 *   RX  fhost core -> net_al_input() [here] -> at_netif_rx_alloc/submit, then free_fn(net_buf)
 *   netif  fhost core -> net_if_add() [here, binds fhost vif <-> our g_sta_if]
 *
 * ★ HW-VERIFY (marked TODO below): these fhost-internal contracts need on-HW iteration —
 *   (a) the TX payload must sit in MAC-DMA-reachable RAM ("shram"/WRAM). We alloc via
 *       platform_malloc (fhost's own allocator); confirm it returns DMA-able memory, else
 *       switch to a WRAM heap (the old wl80211 used a dedicated CONFIG_WRAM_ADDRESS pool).
 *   (b) fhost_tx_req_do()'s exact prototype isn't in a public header — the extern below is a
 *       best-guess from net_al.c's call site; verify arg order/types against a HW run.
 */
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "at_netif.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bflb_sec_trng.h"   /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */

#include "net_al.h"          /* net_al_* / net_if_* / net_buf_tx_* contract + NET_AL_TX_HEADROOM */

/* fhost internal allocator (DMA-capable per fhost's own usage) + the TX submit + wifi_mgmr link
 * query. platform_malloc/free are declared in platform_al.h; fhost_tx_req_do is fhost-internal. */
extern void * platform_malloc( uint32_t size );
extern void   platform_free( void * ptr );
extern int    wifi_mgmr_sta_state_get( void );   /* != 0 when associated */
/* ★ HW-VERIFY (b): best-guess prototype from net_al.c's call site
 *   fhost_tx_req_do(net_if, ibuf, type, cfm_cb, cfm_cb_arg, buf_rx, no_cck) */
extern int fhost_tx_req_do( net_al_if_t net_if, net_al_tx_t net_buf, int type,
                            void * cfm_cb, void * cfm_cb_arg, void * buf_rx, int no_cck );

/* ===== our net interface handle (what fhost's net_al_if_t points at) ======== */
typedef struct
{
    uint8_t hwaddr[ 6 ];
    void *  vif;             /* fhost mac_vif private (from net_if_add) */
} fhost_netif_t;

static fhost_netif_t * g_sta_if;   /* the STA vif's net_if (set by net_if_add, used by prvTx) */

/* ===== TX buffer: [NET_AL_TX_HEADROOM(384) headroom][payload]. fhost prepends the 802.11
 * header into the headroom, then DMAs [header..][payload]. net_al_tx_t is opaque to fhost —
 * it reads it only via net_buf_tx_info(). ======================================= */
typedef struct
{
    uint16_t len;                        /* payload length */
    uint8_t  buf[ NET_AL_TX_HEADROOM ];  /* + payload appended after (flexible via over-alloc) */
} fhost_tx_buf_t;

net_al_tx_t net_buf_tx_alloc_fill( const uint8_t * frame, uint32_t length )
{
    /* ★ HW-VERIFY (a): platform_malloc must be MAC-DMA-reachable (shram/WRAM). */
    fhost_tx_buf_t * b = platform_malloc( sizeof( *b ) + length );
    if( b == NULL )
    {
        return NULL;
    }
    b->len = ( uint16_t ) length;
    ( void ) memcpy( b->buf + NET_AL_TX_HEADROOM, frame, length );
    return ( net_al_tx_t ) b;
}

net_al_tx_t net_buf_tx_alloc( uint32_t length )
{
    fhost_tx_buf_t * b = platform_malloc( sizeof( *b ) + length );
    if( b == NULL )
    {
        return NULL;
    }
    b->len = ( uint16_t ) length;
    return ( net_al_tx_t ) b;
}

net_al_tx_t net_buf_tx_alloc_ref( uint32_t length ) { return net_buf_tx_alloc( length ); }

void net_buf_tx_free( net_al_tx_t net_buf )
{
    platform_free( net_buf );
}

/* Return the single payload segment + the reserved headroom pointer. */
void * net_buf_tx_info( net_al_tx_t * net_buf, uint16_t * tot_len, int * seg_cnt,
                        uint32_t seg_addr[], uint16_t seg_len[], uint32_t * headroom_len )
{
    fhost_tx_buf_t * b = ( fhost_tx_buf_t * ) net_buf;   /* net_al.c treats the ** as the buffer ptr */

    *tot_len      = b->len;
    *seg_cnt      = 1;
    seg_addr[ 0 ] = ( uint32_t ) ( b->buf + NET_AL_TX_HEADROOM );
    seg_len[ 0 ]  = b->len;
    *headroom_len = NET_AL_TX_HEADROOM;
    return b->buf;                                       /* headroom start */
}

bool net_buf_tx_all_shram( net_al_tx_t net_buf )
{
    /* ★ HW-VERIFY (a): true iff the payload is in MAC-DMA RAM. Returning true routes TX straight
     * to fhost_tx_req_do (no bounce). If platform_malloc is NOT DMA-able this must return false —
     * but the FHOST_BUFFER_COPY bounce path lives in the lwip adapter we don't link, so the real
     * fix is to alloc from WRAM. Assume DMA-able for now. */
    ( void ) net_buf;
    return true;
}

/* ===== TX submit ============================================================ */
int net_al_tx_req( struct net_al_tx_req req )
{
    return fhost_tx_req_do( req.net_if, req.net_buf, req.type,
                            req.cfm_cb, req.cfm_cb_arg, req.buf_rx, req.no_cck );
}

void net_al_tx_cfm( void ) { }
void net_al_tx_init( void ) { }
void net_al_tx_do_sta_del( uint8_t sta_id, release_buf_cb release_buf ) { ( void ) sta_id; ( void ) release_buf; }

/* ===== RX up-call: fhost hands us a received ethernet frame ================= */
void net_al_input( net_al_rx_t net_buf, void * payload, net_al_if_t net_if,
                   uint16_t length, uint8_t offset, uint8_t skip_after_eth_hdr,
                   net_buf_free_fn free_fn )
{
    ( void ) net_if;
    uint8_t * eth = ( uint8_t * ) payload + offset;
    uint16_t  len = ( uint16_t ) ( length - skip_after_eth_hdr );

    /* Drop our own broadcast reflected by the AP (src MAC == ours). */
    if( g_sta_if != NULL )
    {
        uint8_t from_us = 1;
        for( int i = 0; i < 6; i++ )
        {
            if( eth[ 6 + i ] != g_sta_if->hwaddr[ i ] ) { from_us = 0; break; }
        }
        if( from_us )
        {
            free_fn( net_buf );
            return;
        }
    }

    NetworkBufferDescriptor_t * pxBuf = at_netif_rx_alloc( ( size_t ) len );
    if( pxBuf != NULL )
    {
        ( void ) memcpy( pxBuf->pucEthernetBuffer, eth + skip_after_eth_hdr, len );
        at_netif_rx_submit( pxBuf, ( size_t ) len );
    }
    else
    {
        iptraceETHERNET_RX_EVENT_LOST();
    }
    free_fn( net_buf );   /* fhost RX buffer copied out — release it */
}

void net_al_rx_resend( bool forward, void * buf, void * payload, int length, int offset, int mac_hdr_len )
{
    ( void ) forward; ( void ) buf; ( void ) payload; ( void ) length; ( void ) offset; ( void ) mac_hdr_len;
}

/* ===== netif lifecycle: fhost creates/binds the interface ==================== */
int net_if_add( net_al_if_t * net_if, const uint8_t * mac_addr, const uint32_t * ipaddr,
                const uint32_t * netmask, const uint32_t * gw, void * vif )
{
    ( void ) ipaddr; ( void ) netmask; ( void ) gw;
    fhost_netif_t * nif = platform_malloc( sizeof( *nif ) );
    if( nif == NULL )
    {
        return -1;
    }
    ( void ) memcpy( nif->hwaddr, mac_addr, 6 );
    nif->vif = vif;
    *net_if  = ( net_al_if_t ) nif;
    if( g_sta_if == NULL ) g_sta_if = nif;   /* STA is the FIRST vif; do NOT overwrite with the AP vif */
    return 0;
}

const uint8_t * net_if_get_mac_addr( net_al_if_t net_if ) { return ( ( fhost_netif_t * ) net_if )->hwaddr; }
void *          net_if_vif_info( net_al_if_t net_if )     { return ( ( fhost_netif_t * ) net_if )->vif; }
net_al_if_t     net_if_find_from_name( const char * name ) { ( void ) name; return ( net_al_if_t ) g_sta_if; }
int             net_if_get_name( net_al_if_t net_if, char * buf, int len ) { ( void ) net_if; if( len > 0 ) buf[ 0 ] = 0; return 0; }
void            net_if_up_cb( net_al_if_t net_if )   { ( void ) net_if; }
void            net_if_down_cb( net_al_if_t net_if ) { ( void ) net_if; }
/* fhost_to_net_if is provided by libfhost (fhost.c) — do not redefine here. */

/* ===== DHCP / IP — FreeRTOS+TCP owns DHCP; these are no-ops/stubs =========== */
void net_al_dhcpd_start( net_al_if_t net_if ) { ( void ) net_if; }
void net_al_dhcpd_stop( net_al_if_t net_if )  { ( void ) net_if; }
int  net_al_ext_dhcp_connect( net_al_if_t net_if )    { ( void ) net_if; return 0; }
int  net_al_ext_dhcp_disconnect( net_al_if_t net_if ) { ( void ) net_if; return 0; }
int  net_al_ext_get_vif_ip( net_al_if_t net_if, uint32_t * ip, uint32_t * mask, uint32_t * gw )
{
    ( void ) net_if; if( ip ) *ip = 0; if( mask ) *mask = 0; if( gw ) *gw = 0; return 0;
}
int  net_al_ext_set_vif_ip( net_al_if_t net_if, uint32_t ip, uint32_t mask, uint32_t gw )
{
    ( void ) net_if; ( void ) ip; ( void ) mask; ( void ) gw; return 0;
}
void net_al_link_set( net_al_if_t net_if, int up ) { ( void ) net_if; ( void ) up; }
void net_al_set_ipv6_enable( net_al_if_t net_if, int en ) { ( void ) net_if; ( void ) en; }

/* ===== net_init + L2 (EAPOL) socket API ====================================
 * fhost/supplicant use these for the WPA handshake. FreeRTOS+TCP owns L3; net_init just marks the
 * IP task ready. net_l2_send is the EAPOL TX (blocking-ish); the L2 socket is the EAPOL RX path.
 * ★ HW-TODO: EAPOL RX routing — net_al_input must divert ethertype 0x888E frames to the supplicant's
 *   L2 socket instead of at_netif (IP). Until then WPA2 association can't complete (open networks OK).
 */
extern void fhost_task_ready( int task );   /* fhost_api.h: mark IP task up (best-guess IP_TASK id) */

int net_init( void )
{
    /* FreeRTOS+TCP is brought up separately (bouffalo_tcp_bringup); nothing to init here. The IP-task-
     * ready handshake (fhost_task_ready(IP_TASK)) is ★ HW-TODO once the IP_TASK id is confirmed. */
    return 0;
}

int net_l2_send( net_al_if_t net_if, const uint8_t * data, int data_len, uint16_t ethertype,
                 const uint8_t * dst_addr, bool * ack )
{
    if( ack != NULL ) { *ack = false; }

    /* Build the ethernet frame (prepend hdr if dst given), copy into a WRAM TX buffer, submit. */
    uint16_t hdr = ( dst_addr != NULL ) ? 14U : 0U;
    fhost_tx_buf_t * b = platform_malloc( sizeof( *b ) + hdr + data_len );
    if( b == NULL )
    {
        return -1;
    }
    uint8_t * p = b->buf + NET_AL_TX_HEADROOM;
    if( dst_addr != NULL )
    {
        ( void ) memcpy( p, dst_addr, 6 );
        if( g_sta_if != NULL ) { ( void ) memcpy( p + 6, g_sta_if->hwaddr, 6 ); }
        p[ 12 ] = ( uint8_t ) ( ethertype >> 8 );
        p[ 13 ] = ( uint8_t ) ( ethertype & 0xFF );
    }
    ( void ) memcpy( p + hdr, data, data_len );
    b->len = ( uint16_t ) ( hdr + data_len );

    struct net_al_tx_req req = { 0 };
    req.net_buf = ( net_al_tx_t ) b;
    req.net_if  = ( net_if != NULL ) ? net_if : ( net_al_if_t ) g_sta_if;
    if( net_al_tx_req( req ) != 0 )
    {
        net_buf_tx_free( ( net_al_tx_t ) b );
        return -2;
    }
    if( ack != NULL ) { *ack = true; }
    return 0;
}

/* ★ HW-TODO: real L2 socket for EAPOL RX (route 0x888E in net_al_input to the supplicant). */
int  net_l2_socket_create( net_al_if_t net_if, uint16_t ethertype ) { ( void ) net_if; ( void ) ethertype; return 0; }
int  net_l2_socket_delete( net_al_if_t net_if ) { ( void ) net_if; return 0; }
void net_buf_tx_cat( net_al_tx_t a, net_al_tx_t b ) { ( void ) a; ( void ) b; }

/* ===== lwip byte-order / ntop shims (fhost/supplicant pull a few; we have no lwip) =========== */
uint16_t lwip_htons( uint16_t n ) { return ( uint16_t ) ( ( n >> 8 ) | ( n << 8 ) ); }
uint32_t lwip_htonl( uint32_t n )
{
    return ( ( n & 0xFF ) << 24 ) | ( ( n & 0xFF00 ) << 8 ) | ( ( n >> 8 ) & 0xFF00 ) | ( ( n >> 24 ) & 0xFF );
}
uint16_t lwip_ntohs( uint16_t n ) { return lwip_htons( n ); }
uint32_t lwip_ntohl( uint32_t n ) { return lwip_htonl( n ); }
const char * lwip_inet_ntop( int af, const void * src, char * dst, uint32_t size )
{
    const uint8_t * a = src;
    ( void ) af;
    ( void ) snprintf( dst, size, "%u.%u.%u.%u", a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ] );
    return dst;
}

/* ===== at_netif platform ops (unchanged app-facing shape) ==================== */
static BaseType_t prvTx( const uint8_t * frame, uint16_t len )
{
    if( g_sta_if == NULL || wifi_mgmr_sta_state_get() == 0 )
    {
        return pdFALSE;   /* not associated yet: fhost has no mac_vif -> TX would crash */
    }
    net_al_tx_t nb = net_buf_tx_alloc_fill( frame, len );
    if( nb == NULL )
    {
        return pdFALSE;
    }
    struct net_al_tx_req req = { 0 };
    req.net_buf = nb;
    req.net_if  = ( net_al_if_t ) g_sta_if;
    if( net_al_tx_req( req ) != 0 )
    {
        net_buf_tx_free( nb );
        return pdFALSE;
    }
    return pdTRUE;
}

static BaseType_t prvLinkUp( void ) { return ( wifi_mgmr_sta_state_get() != 0 ) ? pdTRUE : pdFALSE; }

void at_netif_wifi_mac( uint8_t mac[ 6 ] )
{
    if( g_sta_if != NULL ) { ( void ) memcpy( mac, g_sta_if->hwaddr, 6 ); }
    else { ( void ) memset( mac, 0, 6 ); }
}

static BaseType_t prvGetRandom( uint32_t * out )
{
    return ( bflb_trng_readlen( ( uint8_t * ) out, sizeof( *out ) ) == 0 ) ? pdTRUE : pdFALSE;
}

void at_netif_bt_mac( uint8_t mac[ 6 ] )
{
    extern int mfg_media_read_macaddr_with_lock( uint8_t m[ 6 ], uint8_t reload );
    if( mfg_media_read_macaddr_with_lock( mac, 1 ) != 0 )
    {
        at_netif_wifi_mac( mac );
    }
}

const char * at_netif_platform_name( void ) { return "bl616"; }

static const char * prvHostname( void ) { return at_netif_hostname(); }

static const at_netif_ops_t bouffalo_ops =
{
    .tx         = prvTx,
    .link_up    = prvLinkUp,
    .running    = NULL,
    .mac_get    = at_netif_wifi_mac,
    .get_random = prvGetRandom,
    .hostname   = prvHostname,
    .on_got_ip  = NULL,
};

/* ===== app-facing API (unchanged names) ==================================== */
void bouffalo_freertos_tcp_set_ready( void ) { at_netif_set_ready(); }

void bouffalo_tcp_bringup( void )
{
    at_netif_set_ops( &bouffalo_ops );
    at_netif_bringup();
}
