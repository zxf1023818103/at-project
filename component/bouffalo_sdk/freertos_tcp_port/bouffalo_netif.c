/*
 * FreeRTOS-Plus-TCP <-> Bouffalo wl80211 WiFi driver NetworkInterface + RX/TX
 * bridge (BL616 / BL616CL). DHCP-only IPv4 bring-up: get an IP.
 *
 * This replaces the old lwIP glue (component/bouffalo_sdk/wl80211/lwip.c). The
 * prebuilt libwl80211 references two symbols by name that used to live there:
 *   - wl80211_tcpip_input(): the driver's RX up-call (data + EAPOL frames).
 *   - wl80211_output_raw():  the driver's raw 802.11 mgmt/EAPOL TX.
 * Both are re-supplied here on top of FreeRTOS-Plus-TCP instead of lwIP.
 *
 * Data path:
 *   TX  IP task -> prvOutput() -> wl80211_mac_tx()  (copy into a wifi-RAM block
 *       [tx-desc | frame]; freed in the async txdone callback)
 *   RX  driver -> wl80211_tcpip_input(vif, rxhdr, buf, len, status) [here] ->
 *       copy into a NetworkBufferDescriptor -> xSendEventStructToIPTask(eNetworkRxEvent)
 *
 * Modelled on component/amebad/freertos_tcp_port/realtek_netif.c (the Realtek
 * equivalent). Only the STA VIF is bridged for now (AP/DHCPD comes later).
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"      /* IPStackEvent_t / xSendEventStructToIPTask */
#include "FreeRTOS_Routing.h"         /* NetworkInterface_t / FreeRTOS_AddNetworkInterface */
#include "NetworkBufferManagement.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>                  /* strcasecmp / strncasecmp (mDNS name match) */
#include "bflb_sec_trng.h"           /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */

#include "wl80211.h"                  /* struct iovec, ALIGN4_HI, WL80211_VIF_* enum, wl80211_tcpip_input proto */

/* ---- wl80211 driver entry points (declared extern to avoid pulling the
 * private macsw / platform headers onto this port's include path) ------------ */
extern int  wl80211_mac_tx( uint8_t vif_type, void * desc, unsigned int flags,
                            struct iovec * seg, int seg_cnt, void * txdone_cb, void * opaque );
extern void wl80211_mac_rx_free( void * info );
extern int  platform_get_mac( uint8_t vif_type, uint8_t mac[ 6 ] );
extern void * wl80211_platform_malloc_wram_nolimit( size_t size );
extern void wl80211_platform_free_wram_nolimit( void * ptr );
extern int  wifi_mgmr_sta_state_get( void );   /* != 0 when associated */

/* The wl80211 tx descriptor scratch (struct wl80211_mac_tx_desc) is opaque here;
 * the driver's own _Static_assert bounds it by PBUF_LINK_ENCAPSULATION_HLEN. Reserve
 * that many bytes (ALIGN4) ahead of every TX frame for the driver to build into. */
#define WL80211_TX_DESC_RESERVE   388U

/* Set pdTRUE once FreeRTOS_IPInit_Multi() has run (buffer pool + IP task exist).
 * The driver can deliver RX (beacons/EAPOL) during association — i.e. BEFORE the
 * IP stack is up — so the RX bridge must free-and-drop frames until it is ready,
 * or pxGetNetworkBufferWithDescriptor faults on the uninitialised buffer list. */
volatile BaseType_t xBouffaloTcpReady = pdFALSE;

void bouffalo_freertos_tcp_set_ready( void )
{
    xBouffaloTcpReady = pdTRUE;
}

/* Stored when the interface descriptor is filled; RX descriptors are tagged with
 * it (+ the matching endpoint) so the IP task can route incoming frames. */
static NetworkInterface_t * pxMyInterface = NULL;

#if ( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eProcessBuffer
#else
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eConsiderFrameForProcessing( ( buf ) )
#endif

/* ===== RX bridge =========================================================== *
 * The prebuilt driver calls this directly (by symbol) when a frame arrives:
 * (vif_type, rxhdr, buf, frm_len, status). Unlike the Realtek pull model, the
 * Bouffalo driver PUSHES the frame pointer + length, so we copy it straight into
 * a NetworkBufferDescriptor and free the driver's RX descriptor immediately. */
void wl80211_tcpip_input( uint8_t vif_type, void * rxhdr, void * buf, uint32_t frm_len,
                          uint32_t status )
{
    NetworkBufferDescriptor_t * pxBuf;
    ( void ) status;

    /* Only the STA interface is bridged for now; free + drop everything else. */
    if( ( vif_type != WL80211_VIF_STA ) ||
        ( frm_len == 0U ) || ( frm_len > ( uint32_t ) ( ipconfigNETWORK_MTU + 14 + 4 ) ) ||
        ( xBouffaloTcpReady == pdFALSE ) )
    {
        wl80211_mac_rx_free( rxhdr );
        return;
    }

    pxBuf = pxGetNetworkBufferWithDescriptor( ( size_t ) frm_len, 0 );
    if( pxBuf == NULL )
    {
        wl80211_mac_rx_free( rxhdr );
        iptraceETHERNET_RX_EVENT_LOST();
        return;
    }

    ( void ) memcpy( pxBuf->pucEthernetBuffer, buf, frm_len );
    wl80211_mac_rx_free( rxhdr );          /* driver RX buffer copied out — release it */
    pxBuf->xDataLength = ( size_t ) frm_len;
    /* Tag the descriptor so the IP task can route it (V4.0.0 multi-endpoint). */
    pxBuf->pxInterface = pxMyInterface;
    pxBuf->pxEndPoint  = FreeRTOS_MatchingEndpoint( pxMyInterface, pxBuf->pucEthernetBuffer );

    if( ipCONSIDER_FRAME_FOR_PROCESSING( pxBuf->pucEthernetBuffer ) == eProcessBuffer )
    {
        IPStackEvent_t xRxEvent = { eNetworkRxEvent, pxBuf };
        if( xSendEventStructToIPTask( &xRxEvent, 0 ) == pdFAIL )
        {
            vReleaseNetworkBufferAndDescriptor( pxBuf );
            iptraceETHERNET_RX_EVENT_LOST();
        }
    }
    else
    {
        vReleaseNetworkBufferAndDescriptor( pxBuf );
    }
}

/* ===== TX helper: hand a linear frame to the MAC ============================ *
 * wl80211_mac_tx() takes a writable tx-desc scratch (opaque, reserved ahead of
 * the frame), an iovec of payload segments, and an async txdone callback. We
 * allocate ONE wifi-RAM block [tx-desc | frame], copy the frame in, and free the
 * whole block from the txdone callback. `flags` carries WL80211_MAC_TX_FLAG_MGMT
 * for raw mgmt/EAPOL. Returns 0 on success (block ownership passed to the MAC). */
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
    desc    = block;                               /* driver builds its descriptor here */
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

/* ===== raw 802.11 TX (mgmt / EAPOL) ======================================== *
 * The prebuilt driver calls this by symbol for management + EAPOL frames. It is
 * NOT part of the IP data path (no netif), so it works the same under
 * FreeRTOS-Plus-TCP as under lwIP: copy into wifi RAM and push to the MAC. The
 * caller's completion callback (cb/opaque) is invoked after the MAC txdone. */
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

    /* Layout: [raw_tx_ctx | tx-desc scratch | frame]. The ctx rides in the same
     * block so the txdone callback can fire the caller's cb then free once. */
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

/* ===== NetworkInterface (V4.0.0 multi-endpoint descriptor model) =========== */
static BaseType_t prvInitialise( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    /* The WiFi driver is brought up before the IP task starts; treat the
     * interface as initialised unconditionally (association gates link status). */
    return pdTRUE;
}

static BaseType_t prvGetPhyLinkStatus( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    return ( wifi_mgmr_sta_state_get() != 0 ) ? pdTRUE : pdFALSE;
}

static BaseType_t prvOutput( NetworkInterface_t * pxInterface,
                             NetworkBufferDescriptor_t * const pxNetworkBuffer,
                             BaseType_t xReleaseAfterSend )
{
    BaseType_t xResult = pdFALSE;
    ( void ) pxInterface;

    if( ( pxNetworkBuffer != NULL ) && ( pxNetworkBuffer->pucEthernetBuffer != NULL ) &&
        ( pxNetworkBuffer->xDataLength > 0U ) && ( pxNetworkBuffer->xDataLength <= 0xFFFFU ) )
    {
        /* prvMacTx copies the frame into a wifi-RAM block, so the descriptor is
         * free to be released right away (async DMA reads the copy, not this). */
        if( prvMacTx( WL80211_VIF_STA, pxNetworkBuffer->pucEthernetBuffer,
                      ( uint16_t ) pxNetworkBuffer->xDataLength, 0U ) == 0 )
        {
            xResult = pdTRUE;
            iptraceNETWORK_INTERFACE_TRANSMIT();
        }
    }

    if( xReleaseAfterSend != pdFALSE )
    {
        vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer );
    }
    return xResult;
}

/* Fill + register the interface descriptor. Called before FreeRTOS_IPInit_Multi().
 * xEMACIndex is the WLAN VIF index (0 = STA). */
NetworkInterface_t * pxBouffalo_FillInterfaceDescriptor( BaseType_t xEMACIndex,
                                                         NetworkInterface_t * pxInterface )
{
    ( void ) memset( pxInterface, 0, sizeof( *pxInterface ) );
    pxInterface->pcName             = "wlan0";
    pxInterface->pvArgument         = ( void * ) xEMACIndex;
    pxInterface->pfInitialise       = prvInitialise;
    pxInterface->pfOutput           = prvOutput;
    pxInterface->pfGetPhyLinkStatus = prvGetPhyLinkStatus;
    FreeRTOS_AddNetworkInterface( pxInterface );
    pxMyInterface = pxInterface;
    return pxInterface;
}

/* Read the STA MAC (for FreeRTOS_FillEndPoint). Returns 0 on success. */
int bouffalo_wlan_get_mac( uint8_t mac[ 6 ] )
{
    return platform_get_mac( WL80211_VIF_STA, mac );
}

/* ===== Application hooks the stack requires ================================ */
/* Random for the DHCP xid / TCP ISN: the BL616 hardware TRNG (bflb_sec_trng, the
 * same engine the mbedTLS entropy port uses). bflb_trng_readlen returns 0 on OK. */
BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    return ( bflb_trng_readlen( ( uint8_t * ) pulNumber, sizeof( *pulNumber ) ) == 0 ) ? pdTRUE : pdFALSE;
}

uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress,
                                             uint16_t usSourcePort,
                                             uint32_t ulDestinationAddress,
                                             uint16_t usDestinationPort )
{
    uint32_t v = 0;
    ( void ) ulSourceAddress; ( void ) usSourcePort;
    ( void ) ulDestinationAddress; ( void ) usDestinationPort;
    ( void ) bflb_trng_readlen( ( uint8_t * ) &v, sizeof( v ) );
    return v;
}

const char * pcApplicationHostnameHook( void )
{
    static char host[ 24 ] = { 0 };

    if( host[ 0 ] == '\0' )
    {
        uint8_t mac[ 6 ] = { 0 };
        ( void ) platform_get_mac( WL80211_VIF_STA, mac );
        ( void ) snprintf( host, sizeof( host ), "bl616_%02x%02x%02x", mac[ 3 ], mac[ 4 ], mac[ 5 ] );
    }
    return host;
}

BaseType_t xApplicationDNSQueryHook( const char * pcName )
{
    const char * h  = pcApplicationHostnameHook();
    size_t       hl = strlen( h );

    if( strcasecmp( pcName, h ) == 0 )
    {
        return pdPASS;
    }
    if( ( strncasecmp( pcName, h, hl ) == 0 ) && ( strcasecmp( &pcName[ hl ], ".local" ) == 0 ) )
    {
        return pdPASS;
    }
    return pdFAIL;
}

/* ===== One-shot bring-up (called from the app once the WiFi driver is up) === *
 * Registers the STA interface + a DHCP IPv4 endpoint and starts the IP task.
 * Idempotent. After this the RX bridge is armed (set_ready) so frames delivered
 * during association reach the stack. */
void bouffalo_tcp_bringup( void )
{
    static NetworkInterface_t xInterface;
    static NetworkEndPoint_t  xEndPoint;
    static BaseType_t         xDone = pdFALSE;
    uint8_t ucMAC[ 6 ] = { 0 };
    static const uint8_t ucZero[ 4 ] = { 0, 0, 0, 0 };   /* IP/mask/gw/dns = 0 -> DHCP */

    if( xDone != pdFALSE )
    {
        return;
    }
    xDone = pdTRUE;

    ( void ) platform_get_mac( WL80211_VIF_STA, ucMAC );

    pxBouffalo_FillInterfaceDescriptor( 0, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucZero, ucZero, ucZero, ucZero, ucMAC );
    xEndPoint.bits.bWantDHCP = pdTRUE_UNSIGNED;

    FreeRTOS_IPInit_Multi();
    bouffalo_freertos_tcp_set_ready();
}

/* Network up/down: print the leased IP. (CODE_WIFI_ON_GOT_IP event wiring can be
 * layered on once DHCP is proven; the app polls FreeRTOS_IsNetworkUp meanwhile.)
 * Single-arg hook — the config uses ipconfigIPv4_BACKWARD_COMPATIBLE. */
extern int printf( const char *, ... );

void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    if( eNetworkEvent == eNetworkUp )
    {
        NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );
        uint32_t ulIP = ( px != NULL ) ? px->ipv4_settings.ulIPAddress : 0U;
        printf( "[TCP] network up: %u.%u.%u.%u\r\n",
                ( unsigned ) ( ulIP & 0xffU ), ( unsigned ) ( ( ulIP >> 8 ) & 0xffU ),
                ( unsigned ) ( ( ulIP >> 16 ) & 0xffU ), ( unsigned ) ( ( ulIP >> 24 ) & 0xffU ) );
    }
    else
    {
        printf( "[TCP] network down\r\n" );
    }
}
