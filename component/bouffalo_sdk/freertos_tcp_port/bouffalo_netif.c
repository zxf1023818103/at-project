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
 * HW-verified constraints (both bit on real HW before being fixed):
 *   (a) TX payloads MUST come from the WRAM heap (MM_FLAG_HEAP_WRAM_0) — the MAC DMA cannot
 *       reach the normal heap; platform_malloc (= kmalloc(...,0)) trips
 *       "ASSERT REC at hal_machw_gen_handler" mid-TX.
 *   (b) TX submission goes through fhost_tx_start() (staid resolve + queueing), same as the
 *       lwip adapter — NOT a bare fhost_tx_req_do(), which is only the internal resubmit hook
 *       reached back through net_al_tx_req().
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "FreeRTOS_IP.h"
#include "at_netif.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bflb_sec_trng.h"   /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */

#include "net_al.h"          /* net_al_* / net_if_* / net_buf_tx_* contract + NET_AL_TX_HEADROOM */
#include "mm.h"              /* kmalloc(MM_FLAG_HEAP_WRAM_0) — MAC-DMA-reachable WRAM heap */

/* fhost internal allocator (DMA-capable per fhost's own usage) + the TX submit + wifi_mgmr link
 * query. platform_malloc/free are declared in platform_al.h; fhost_tx_req_do is fhost-internal. */
extern void * platform_malloc( uint32_t size );
extern void   platform_free( void * ptr );
extern int    wifi_mgmr_sta_state_get( void );   /* != 0 when associated */
/* fhost_tx_req_do: internal resubmit hook — only reached from fhost core via net_al_tx_req
 * (prototype per the lwip adapter's own extern, wifi6_lwip_adapter/net_al.c:57). */
extern int fhost_tx_req_do( net_al_if_t net_if, net_al_tx_t net_buf, int type,
                            void * cfm_cb, void * cfm_cb_arg, void * buf_rx, int no_cck );
/* fhost_tx_start: the proper TX entry point (staid resolve + queueing). Exported by libfhost;
 * prototype per wifi6_lwip_adapter/net_al.c:54, cb type per fhost_api.h:692 (cb_fhost_tx). */
extern int fhost_tx_start( net_al_if_t net_if, net_al_tx_t net_buf,
                           void ( * cfm_cb )( uint32_t frame_id, bool acknowledged, void * arg ),
                           void * cfm_cb_arg );

/* wpa_supplicant eloop (eloop_rtos.c, in libwpa_supplicant.a) — pure RTOS queue, no sockets.
 * eloop_event_commit COPIES the payload (os_malloc+memcpy), so the RX buffer can be freed
 * right after the call. Event ids per eloop_rtos.h: wl1 -> 10, wl2 -> 11, fallback 3. */
extern int eloop_event_commit( int type, const char * request, int req_len );
#define ELOOP_EVT_WPA_L2_DATA        3
#define ELOOP_EVT_WPA_L2_DATA_WL1    10
#define ELOOP_EVT_WPA_L2_DATA_WL2    11

/* ===== our net interface handle (what fhost's net_al_if_t points at) ======== */
typedef struct
{
    uint8_t hwaddr[ 6 ];
    void *  vif;             /* fhost mac_vif private (from net_if_add) */
    char    name[ 4 ];       /* "wl1" (STA, first vif) / "wl2" (AP, second) — supplicant looks up by name */
} fhost_netif_t;

static fhost_netif_t * g_sta_if;   /* the STA vif's net_if (set by net_if_add, used by prvTx) */
static fhost_netif_t * g_ap_if;    /* the AP vif's net_if (second net_if_add) */

/* ===== L2 (EAPOL) ethertype filters: which RX frames go to the supplicant's eloop instead of
 * the IP stack. Registered by l2_packet_init -> net_l2_socket_create. Same shape as the lwip
 * adapter's l2_filter[] (wifi6_lwip_adapter/net_al.c). ======================== */
#define L2_FILTER_COUNT    2
static struct
{
    net_al_if_t net_if;
    uint16_t    ethertype;
} l2_filter[ L2_FILTER_COUNT ];

/* ===== TX buffer: [NET_AL_TX_HEADROOM(384) headroom][payload]. fhost prepends the 802.11
 * header into the headroom, then DMAs [header..][payload]. net_al_tx_t is opaque to fhost —
 * it reads it only via net_buf_tx_info(). ======================================= */
typedef struct
{
    uint32_t len;                        /* payload length. 32-bit ON PURPOSE: keeps buf[] (and the
                                          * 384B headroom, a multiple of 4) 4-aligned — fhost does
                                          * word stores into the headroom (HW: "Store/AMO address
                                          * misaligned" @ +2 with a uint16_t here). */
    uint8_t  buf[ NET_AL_TX_HEADROOM ];  /* + payload appended after (flexible via over-alloc) */
} fhost_tx_buf_t;

net_al_tx_t net_buf_tx_alloc_fill( const uint8_t * frame, uint32_t length )
{
    /* MUST come from the WRAM heap: the MAC DMA cannot reach the normal heap (kmalloc(...,0)
     * payloads trip "ASSERT REC at hal_machw_gen_handler" — HW-verified). Same constraint the
     * bl602 wl80211 port had (TX blocks in ram_wifi). */
    fhost_tx_buf_t * b = kmalloc( sizeof( *b ) + length, MM_FLAG_HEAP_WRAM_0 );
    if( b == NULL )
    {
        return NULL;
    }
    b->len = length;
    ( void ) memcpy( b->buf + NET_AL_TX_HEADROOM, frame, length );
    return ( net_al_tx_t ) b;
}

net_al_tx_t net_buf_tx_alloc( uint32_t length )
{
    fhost_tx_buf_t * b = kmalloc( sizeof( *b ) + length, MM_FLAG_HEAP_WRAM_0 );
    if( b == NULL )
    {
        return NULL;
    }
    b->len = length;
    return ( net_al_tx_t ) b;
}

net_al_tx_t net_buf_tx_alloc_ref( uint32_t length ) { return net_buf_tx_alloc( length ); }

void net_buf_tx_free( net_al_tx_t net_buf )
{
    kfree( net_buf );
}

/* Return the single payload segment + the reserved headroom pointer. */
void * net_buf_tx_info( net_al_tx_t * net_buf, uint16_t * tot_len, int * seg_cnt,
                        uint32_t seg_addr[], uint16_t seg_len[], uint32_t * headroom_len )
{
    fhost_tx_buf_t * b = ( fhost_tx_buf_t * ) net_buf;   /* net_al.c treats the ** as the buffer ptr */

    *tot_len      = ( uint16_t ) b->len;
    *seg_cnt      = 1;
    seg_addr[ 0 ] = ( uint32_t ) ( b->buf + NET_AL_TX_HEADROOM );
    seg_len[ 0 ]  = b->len;
    *headroom_len = NET_AL_TX_HEADROOM;
    return b->buf;                                       /* headroom start */
}

bool net_buf_tx_all_shram( net_al_tx_t net_buf )
{
    /* True: all TX buffers come from the WRAM heap (MAC-DMA reachable), so fhost may DMA
     * directly with no bounce copy. */
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
    uint8_t * eth = ( uint8_t * ) payload + offset;
    uint16_t  len = ( uint16_t ) ( length - skip_after_eth_hdr );

    /* Make the frame contiguous: move the 14B eth header down over the skip gap (same trick as
     * the lwip adapter's net_al_input; regions may overlap, hence memmove). */
    if( skip_after_eth_hdr != 0 )
    {
        ( void ) memmove( eth + skip_after_eth_hdr, eth, 14 );
        eth += skip_after_eth_hdr;
    }

    /* L2 filter: divert EAPOL (0x888E) etc. to the supplicant's eloop, not the IP stack.
     * The WPA2/WPA3 handshake dies without this. eloop_event_commit copies the frame. */
    if( len >= 14 )
    {
        uint16_t ethertype = ( uint16_t ) ( ( eth[ 12 ] << 8 ) | eth[ 13 ] );
        for( int i = 0; i < L2_FILTER_COUNT; i++ )
        {
            if( ( l2_filter[ i ].net_if == net_if ) && ( l2_filter[ i ].ethertype == ethertype ) )
            {
                fhost_netif_t * nif = ( fhost_netif_t * ) net_if;
                int event_id = ( nif == g_ap_if ) ? ELOOP_EVT_WPA_L2_DATA_WL2
                                                  : ELOOP_EVT_WPA_L2_DATA_WL1;
                if( eloop_event_commit( event_id, ( const char * ) eth, ( int ) len ) != 0 )
                {
                    printf( "netif: L2 eloop commit failed (ev %d len %u)\r\n", event_id, len );
                }
                free_fn( net_buf );
                return;
            }
        }
    }

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
        ( void ) memcpy( pxBuf->pucEthernetBuffer, eth, len );
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
    /* fhost adds the STA vif first ("wl1"), then the AP vif ("wl2") — the supplicant finds
     * them by these names (net_if_find_from_name / eloop_get_l2_event_id). */
    if( g_sta_if == NULL )     { g_sta_if = nif; ( void ) strcpy( nif->name, "wl1" ); }
    else if( g_ap_if == NULL ) { g_ap_if = nif;  ( void ) strcpy( nif->name, "wl2" ); }
    else                       { nif->name[ 0 ] = 0; }
    return 0;
}

const uint8_t * net_if_get_mac_addr( net_al_if_t net_if ) { return ( ( fhost_netif_t * ) net_if )->hwaddr; }
void *          net_if_vif_info( net_al_if_t net_if )     { return ( ( fhost_netif_t * ) net_if )->vif; }

net_al_if_t net_if_find_from_name( const char * name )
{
    if( name != NULL )
    {
        if( ( g_sta_if != NULL ) && ( strcmp( name, g_sta_if->name ) == 0 ) ) { return ( net_al_if_t ) g_sta_if; }
        if( ( g_ap_if != NULL ) && ( strcmp( name, g_ap_if->name ) == 0 ) )   { return ( net_al_if_t ) g_ap_if; }
    }
    return NULL;
}

/* Contract (net_al.h): copy the name (incl. '\0' if it fits); return strlen(name), <0 on error. */
int net_if_get_name( net_al_if_t net_if, char * buf, int len )
{
    if( net_if == NULL ) { return -1; }
    const char * name = ( ( fhost_netif_t * ) net_if )->name;
    int n = ( int ) strlen( name );
    if( len > 0 )
    {
        ( void ) memcpy( buf, name, ( n + 1 <= len ) ? ( size_t ) ( n + 1 ) : ( size_t ) len );
    }
    return n;
}
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
 * IP task ready. net_l2_send is the EAPOL TX (blocking-ish); RX goes net_al_input -> l2_filter
 * match -> eloop_event_commit (see above) into the supplicant's eloop task.
 */
extern void fhost_task_ready( int task );   /* fhost_api.h: mark IP task up (best-guess IP_TASK id) */

int net_init( void )
{
    /* FreeRTOS+TCP is brought up separately (bouffalo_tcp_bringup); nothing to init here. The IP-task-
     * ready handshake (fhost_task_ready(IP_TASK)) is ★ HW-TODO once the IP_TASK id is confirmed. */
    return 0;
}

/* Blocking TX confirm for net_l2_send (EAPOL). Mirrors the lwip adapter: one in-flight L2
 * frame at a time, cfm callback stores the ack and wakes the sender. */
static SemaphoreHandle_t l2_send_sem;
static void net_l2_send_cfm( uint32_t frame_id, bool acknowledged, void * arg )
{
    ( void ) frame_id;
    if( arg != NULL ) { *( ( bool * ) arg ) = acknowledged; }
    ( void ) xSemaphoreGive( l2_send_sem );
}

int net_l2_send( net_al_if_t net_if, const uint8_t * data, int data_len, uint16_t ethertype,
                 const uint8_t * dst_addr, bool * ack )
{
    if( ack != NULL ) { *ack = false; }
    if( l2_send_sem == NULL )
    {
        l2_send_sem = xSemaphoreCreateBinary();   /* created once, on the supplicant task */
        if( l2_send_sem == NULL ) { return -1; }
    }

    /* Build the ethernet frame (prepend hdr if dst given), copy into a WRAM TX buffer, submit. */
    uint16_t hdr = ( dst_addr != NULL ) ? 14U : 0U;
    fhost_tx_buf_t * b = kmalloc( sizeof( *b ) + hdr + data_len, MM_FLAG_HEAP_WRAM_0 );
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
    b->len = ( uint32_t ) ( hdr + data_len );

    /* Blocking: submit via fhost_tx_start with a confirm cb, wait for the TX completion
     * (the supplicant retries on !ack — see net_l2_send in the lwip adapter). */
    int res = fhost_tx_start( ( net_if != NULL ) ? net_if : ( net_al_if_t ) g_sta_if,
                              ( net_al_tx_t ) b, net_l2_send_cfm, ack );
    if( res != 0 )
    {
        net_buf_tx_free( ( net_al_tx_t ) b );
        return -2;
    }
    if( xSemaphoreTake( l2_send_sem, pdMS_TO_TICKS( 2000 ) ) != pdTRUE )
    {
        printf( "netif: L2 TX confirm timeout\r\n" );
    }
    return 0;
}

/* L2 "socket" = an ethertype filter slot; RX delivery happens in net_al_input -> eloop.
 * Same semantics as the lwip adapter (no real socket object; caller ignores the fd). */
int net_l2_socket_create( net_al_if_t net_if, uint16_t ethertype )
{
    int free_slot = -1;
    for( int i = 0; i < L2_FILTER_COUNT; i++ )
    {
        if( ( l2_filter[ i ].net_if == net_if ) && ( l2_filter[ i ].ethertype == ethertype ) )
        {
            return -1;   /* already registered */
        }
        if( ( l2_filter[ i ].net_if == NULL ) && ( free_slot < 0 ) )
        {
            free_slot = i;
        }
    }
    if( free_slot < 0 )
    {
        return -1;
    }
    l2_filter[ free_slot ].net_if    = net_if;
    l2_filter[ free_slot ].ethertype = ethertype;
    return 0;
}

int net_l2_socket_delete( net_al_if_t net_if )
{
    for( int i = 0; i < L2_FILTER_COUNT; i++ )
    {
        if( l2_filter[ i ].net_if == net_if )
        {
            l2_filter[ i ].net_if    = NULL;
            l2_filter[ i ].ethertype = 0;
            return 0;
        }
    }
    return -1;
}

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

/* ===== TX-wedge watchdog =====================================================
 * The macsw MAC core intermittently wedges its TX AC queues (frames stay "transmitting"
 * forever, "internal frame left:N" + hal_machw asserts; reproduced with the stock SDK
 * example too — pre-built firmware, not fixable here). Detect it from the data path:
 * every prvTx submit carries a confirm callback; if confirms stop while frames are
 * in flight, force a WiFi reassociation (fhost flushes/cfms pending TX on sta_del and
 * wifi_service auto-reconnects; MQTT/OTA layers resume on their own). */
#define TX_WEDGE_MIN_INFLIGHT    1                          /* any un-confirmed frame counts: once the
                                                             * MAC wedges, fhost's credits run out and
                                                             * fhost_tx_start starts FAILING, so the
                                                             * in-flight count stops growing */
#define TX_WEDGE_STALL_TICKS     pdMS_TO_TICKS( 12000 )
#define TX_WEDGE_MAX_SUBMIT_FAIL 16                         /* consecutive fhost_tx_start failures */
static volatile uint32_t   s_tx_outstanding;
static volatile uint32_t   s_tx_submit_fail;
static volatile TickType_t s_tx_last_cfm;
static volatile uint8_t    s_tx_wedge_kick;

static void prvTxCfm( uint32_t frame_id, bool acknowledged, void * arg )
{
    ( void ) frame_id; ( void ) acknowledged; ( void ) arg;
    if( s_tx_outstanding > 0 ) { s_tx_outstanding--; }
    s_tx_last_cfm = xTaskGetTickCount();
}

static void prvWedgeRecoveryTask( void * arg )
{
    ( void ) arg;
    extern int wifi_sta_disconnect( void );
    printf( "netif: TX wedge (in-flight %u, no cfm) -> WiFi reassociate\r\n",
            ( unsigned ) s_tx_outstanding );
    ( void ) wifi_sta_disconnect();   /* DISCONNECT event -> wifi_service reconnects */
    s_tx_outstanding = 0;
    s_tx_submit_fail = 0;
    s_tx_wedge_kick  = 0;
    vTaskDelete( NULL );
}

/* ===== at_netif platform ops (unchanged app-facing shape) ==================== */
static BaseType_t prvTx( const uint8_t * frame, uint16_t len )
{
    if( g_sta_if == NULL || wifi_mgmr_sta_state_get() == 0 )
    {
        return pdFALSE;   /* not associated yet: fhost has no mac_vif -> TX would crash */
    }

    /* Wedge check on the way in: un-confirmed frames stalled for a long while, or fhost
     * refusing submissions outright (credits exhausted by the wedged MAC queue). */
    TickType_t now = xTaskGetTickCount();
    if( ( s_tx_wedge_kick == 0 ) &&
        ( ( ( s_tx_outstanding >= TX_WEDGE_MIN_INFLIGHT ) &&
            ( ( now - s_tx_last_cfm ) > TX_WEDGE_STALL_TICKS ) ) ||
          ( s_tx_submit_fail >= TX_WEDGE_MAX_SUBMIT_FAIL ) ) )
    {
        s_tx_wedge_kick = 1;
        if( xTaskCreate( prvWedgeRecoveryTask, "txwedge", 512, NULL,
                         tskIDLE_PRIORITY + 1, NULL ) != pdPASS )
        {
            s_tx_wedge_kick = 0;
        }
    }

    net_al_tx_t nb = net_buf_tx_alloc_fill( frame, len );
    if( nb == NULL )
    {
        return pdFALSE;
    }
    /* fhost_tx_start = the proper TX entry (staid resolve + queueing), same as the lwip
     * adapter's low_level_output. It frees the buffer on failure paths it owns; on a
     * non-zero return the buffer was not consumed. */
    if( s_tx_outstanding == 0 ) { s_tx_last_cfm = now; }   /* re-arm the stall clock */
    s_tx_outstanding++;
    if( fhost_tx_start( ( net_al_if_t ) g_sta_if, nb, prvTxCfm, NULL ) != 0 )
    {
        if( s_tx_outstanding > 0 ) { s_tx_outstanding--; }
        s_tx_submit_fail++;
        net_buf_tx_free( nb );
        return pdFALSE;
    }
    s_tx_submit_fail = 0;
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
