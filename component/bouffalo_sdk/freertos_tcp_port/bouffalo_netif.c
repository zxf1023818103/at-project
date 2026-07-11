/*
 * FreeRTOS-Plus-TCP <-> Bouffalo wl80211 driver bridge (BL616 / BL616CL). The shared
 * NetworkInterface boilerplate lives in component/freertos-plus-tcp_config/at_netif.c;
 * this file is the wl80211 driver glue: the RX up-call + the raw/data TX the prebuilt
 * libwl80211 references by symbol, plus the ops.
 *
 * Data path:
 *   TX  IP task -> at_netif prvOutput -> ops.tx -> prvMacTx() (wifi-RAM [desc|frame])
 *   RX  driver -> wl80211_tcpip_input() [here] -> at_netif_rx_alloc/submit
 */
#include "FreeRTOS.h"
#include "FreeRTOS_IP.h"
#include "at_netif.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bflb_sec_trng.h"           /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */
#include "wl80211.h"                 /* struct iovec, ALIGN4_HI, WL80211_VIF_*, wl80211_tcpip_input proto */

extern int  wl80211_mac_tx( uint8_t vif_type, void * desc, unsigned int flags,
                            struct iovec * seg, int seg_cnt, void * txdone_cb, void * opaque );
extern void wl80211_mac_rx_free( void * info );
extern int  platform_get_mac( uint8_t vif_type, uint8_t mac[ 6 ] );
extern void * wl80211_platform_malloc_wram_nolimit( size_t size );
extern void wl80211_platform_free_wram_nolimit( void * ptr );
extern int  wifi_mgmr_sta_state_get( void );   /* != 0 when associated */

/* Reserve (ALIGN4) the opaque wl80211 tx-desc scratch ahead of every TX frame. */
#define WL80211_TX_DESC_RESERVE   388U

/* ===== RX bridge (driver PUSHES the frame; symbol referenced by libwl80211) = */
void wl80211_tcpip_input( uint8_t vif_type, void * rxhdr, void * buf, uint32_t frm_len,
                          uint32_t status )
{
    NetworkBufferDescriptor_t * pxBuf;
    ( void ) status;

    /* Only STA is bridged for now; and drop until the stack is up. Still free the
     * driver RX desc in every path or its RX ring stalls. */
    pxBuf = ( vif_type == WL80211_VIF_STA ) ? at_netif_rx_alloc( ( size_t ) frm_len ) : NULL;
    if( pxBuf == NULL )
    {
        wl80211_mac_rx_free( rxhdr );
        if( vif_type == WL80211_VIF_STA )
        {
            iptraceETHERNET_RX_EVENT_LOST();
        }
        return;
    }

    ( void ) memcpy( pxBuf->pucEthernetBuffer, buf, frm_len );
    wl80211_mac_rx_free( rxhdr );                       /* copied out — release driver RX buffer */
    at_netif_rx_submit( pxBuf, ( size_t ) frm_len );
}

/* ===== TX helper: hand a linear frame to the MAC =========================== */
static void prvTxDoneFree( void * opaque )
{
    wl80211_platform_free_wram_nolimit( opaque );
}

static int prvMacTx( uint8_t vif_type, const void * frame, uint16_t len, unsigned int flags )
{
    uint8_t * block;
    void    * desc;
    uint8_t * payload;
    struct iovec seg[ 1 ];

    block = wl80211_platform_malloc_wram_nolimit( WL80211_TX_DESC_RESERVE + ALIGN4_HI( len ) );
    if( block == NULL )
    {
        return -1;
    }
    desc    = block;
    payload = block + WL80211_TX_DESC_RESERVE;
    ( void ) memcpy( payload, frame, len );

    seg[ 0 ].iov_base = payload;
    seg[ 0 ].iov_len  = len;

    if( wl80211_mac_tx( vif_type, desc, flags, seg, 1, ( void * ) prvTxDoneFree, block ) != 0 )
    {
        wl80211_platform_free_wram_nolimit( block );
        return -2;
    }
    return 0;
}

/* ===== raw 802.11 TX (mgmt / EAPOL) — driver calls this by symbol =========== */
struct raw_tx_ctx
{
    void ( *cb )( void * );
    void * opaque;
    void * block;
};

static void prvRawTxDone( void * ctx_ )
{
    struct raw_tx_ctx * ctx = ctx_;
    if( ctx->cb != NULL )
    {
        ctx->cb( ctx->opaque );
    }
    wl80211_platform_free_wram_nolimit( ctx->block );
}

int wl80211_output_raw( uint8_t vif_type, void * buffer, uint16_t len, unsigned int flags,
                        void ( *cb )( void * ), void * opaque )
{
    uint8_t * block;
    struct raw_tx_ctx * ctx;
    uint8_t * payload;
    struct iovec seg[ 1 ];

    block = wl80211_platform_malloc_wram_nolimit( ALIGN4_HI( sizeof( *ctx ) ) +
                                                  WL80211_TX_DESC_RESERVE + ALIGN4_HI( len ) );
    if( block == NULL )
    {
        return -1;
    }
    ctx         = ( struct raw_tx_ctx * ) block;
    ctx->cb     = cb;
    ctx->opaque = opaque;
    ctx->block  = block;

    void * desc = block + ALIGN4_HI( sizeof( *ctx ) );
    payload     = ( uint8_t * ) desc + WL80211_TX_DESC_RESERVE;
    ( void ) memcpy( payload, buffer, len );

    seg[ 0 ].iov_base = payload;
    seg[ 0 ].iov_len  = len;

    if( wl80211_mac_tx( vif_type, desc, flags, seg, 1, ( void * ) prvRawTxDone, ctx ) != 0 )
    {
        wl80211_platform_free_wram_nolimit( block );
        return -3;
    }
    return 0;
}

/* ===== platform ops ======================================================== */
static BaseType_t prvTx( const uint8_t * frame, uint16_t len )
{
    return ( prvMacTx( WL80211_VIF_STA, frame, len, 0U ) == 0 ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvLinkUp( void )     { return ( wifi_mgmr_sta_state_get() != 0 ) ? pdTRUE : pdFALSE; }
static void prvMacGet( uint8_t mac[ 6 ] ) { ( void ) platform_get_mac( WL80211_VIF_STA, mac ); }

static BaseType_t prvGetRandom( uint32_t * out )
{
    return ( bflb_trng_readlen( ( uint8_t * ) out, sizeof( *out ) ) == 0 ) ? pdTRUE : pdFALSE;
}

/* BT MAC (== the BLE BD_ADDR basis, matching amebad's efuse 0x190): mfg_media_read_macaddr_with_lock
 * is the raw efuse/mfg MAC the Bouffalo BT controller loads as its public address (WiFi + BT share
 * this one efuse field). It lives in the std lib (always linked, no BLE image needed); on a
 * blank-MAC board it returns non-zero and we fall back to the WiFi STA MAC. */
static void prvHostMac( uint8_t mac[ 6 ] )
{
    extern int mfg_media_read_macaddr_with_lock( uint8_t m[ 6 ], uint8_t reload );
    if( mfg_media_read_macaddr_with_lock( mac, 1 ) != 0 )
    {
        ( void ) platform_get_mac( WL80211_VIF_STA, mac );
    }
}

static const char * prvHostname( void )
{
    return at_netif_hostname( "bl616", prvHostMac );   /* HOSTNAME override else bl616_<mac> */
}

static const at_netif_ops_t bouffalo_ops =
{
    .tx         = prvTx,
    .link_up    = prvLinkUp,
    .running    = NULL,
    .mac_get    = prvMacGet,
    .get_random = prvGetRandom,
    .hostname   = prvHostname,
    .on_got_ip  = NULL,
};

/* Read the STA MAC (kept: some callers use it by name). Returns 0 on success. */
int bouffalo_wlan_get_mac( uint8_t mac[ 6 ] ) { return platform_get_mac( WL80211_VIF_STA, mac ); }

/* ===== app-facing API (unchanged names) ==================================== */
void bouffalo_freertos_tcp_set_ready( void ) { at_netif_set_ready(); }

void bouffalo_tcp_bringup( void )
{
    at_netif_set_ops( &bouffalo_ops );
    at_netif_bringup();
}
