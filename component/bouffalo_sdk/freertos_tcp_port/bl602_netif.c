/*
 * FreeRTOS-Plus-TCP <-> Bouffalo bl60x (BL602) WiFi driver NetworkInterface +
 * RX/TX bridge. DHCP-only IPv4 STA bring-up: get an IP.
 *
 * Replaces the lwIP netif/pbuf glue that used to be woven through the wifi4
 * driver (wifi.c wifi_tx / bl606a0_wifi_netif_init, bl_utils.c tcpip_stack_input
 * netif->input, wifi_mgmr_ext.c netifapi_netif_add). The driver's firmware-facing
 * TX descriptor path (bl_output/bl_tx_push/bl_tx_cfm in bl_tx.c) is rewritten to
 * carry a plain [tx-desc | frame] block instead of a pbuf; the RX path in
 * bl_utils.c copies frames into NetworkBufferDescriptors via the helpers here.
 *
 * Data path:
 *   TX  IP task -> prvOutput() -> bl602_ll_output() [bl_tx.c] -> firmware
 *   RX  driver tcpip_stack_input() [bl_utils.c] -> bl602_tcp_rx_alloc/submit()
 *       [here] -> xSendEventStructToIPTask(eNetworkRxEvent)
 *
 * BL602 only. Modelled on bouffalo_netif.c (the wl80211 sibling).
 */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_Routing.h"
#include "NetworkBufferManagement.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "bflb_sec_trng.h"    /* bflb_trng_readlen — HW TRNG for the DHCP xid / TCP ISN */

/* wl80211-style event post for CODE_WIFI_ON_GOT_IP (bflb async event). Declared
 * extern to avoid pulling the driver headers; matches platform_post_event. */
extern int  platform_post_event( int type, int code, int code2 );
extern int  wifi_mgmr_sta_state_get( void );          /* != 0 when associated */
extern int  wifi_mgmr_sta_mac_get( uint8_t mac[ 6 ] );
extern int  printf( const char *, ... );

/* Firmware TX entry (bl_tx.c): send one linear ethernet frame. cfm = NULL uses the
 * driver default tx-done callback. Returns 0 on OK. */
extern int  bl602_ll_output( int is_sta, void * frame, uint16_t len, void * cfm );

/* EV_WIFI / CODE_WIFI_ON_GOT_IP numeric values (from the bflb wifi event enum;
 * kept in sync with the driver -- must match wifi_mgmr_ext.h so the app's
 * wifi_event_handler routes it to the CODE_WIFI_ON_GOT_IP case instead of
 * printing "Unknown code". CODE_WIFI_ON_GOT_IP == 7 (wifi_mgmr_ext.h:19). */
#ifndef EV_WIFI
#define EV_WIFI                 1
#endif
#ifndef CODE_WIFI_ON_GOT_IP
#define CODE_WIFI_ON_GOT_IP     7
#endif

volatile BaseType_t xBl602TcpReady = pdFALSE;

void bl602_freertos_tcp_set_ready( void )
{
    xBl602TcpReady = pdTRUE;
}

static NetworkInterface_t * pxMyInterface = NULL;

#if ( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eProcessBuffer
#else
    #define ipCONSIDER_FRAME_FOR_PROCESSING( buf )    eConsiderFrameForProcessing( ( buf ) )
#endif

/* ===== RX helpers (called from bl_utils.c tcpip_stack_input) =============== *
 * The driver assembles the (possibly fragmented) frame into the NetworkBuffer it
 * gets from bl602_tcp_rx_alloc(), then submits it. Returns NULL if the stack is
 * not up yet or the pool is exhausted (driver then drops + frees its RX desc). */
NetworkBufferDescriptor_t * bl602_tcp_rx_alloc( uint32_t len )
{
    if( ( xBl602TcpReady == pdFALSE ) || ( len == 0U ) ||
        ( len > ( uint32_t ) ( ipconfigNETWORK_MTU + 14 + 4 ) ) )
    {
        return NULL;
    }
    return pxGetNetworkBufferWithDescriptor( ( size_t ) len, 0 );
}

void bl602_tcp_rx_submit( NetworkBufferDescriptor_t * pxBuf, uint32_t len )
{
    if( pxBuf == NULL )
    {
        return;
    }
    pxBuf->xDataLength = ( size_t ) len;
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

/* ===== NetworkInterface ===================================================== */
static BaseType_t prvInitialise( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    return pdTRUE;
}

static BaseType_t prvGetPhyLinkStatus( NetworkInterface_t * pxInterface )
{
    ( void ) pxInterface;
    /* wifi_mgmr_state_get() fills WIFI_STATE_*; link is up once the STA has
     * associated (0x03 = CONNECTED_IP_GETTING so DHCP can run, 0x04 = IP_GOT). */
    extern int wifi_mgmr_state_get( int * state );
    int state = 0;
    wifi_mgmr_state_get( &state );
    return ( state == 0x03 || state == 0x04 ) ? pdTRUE : pdFALSE;
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
        /* bl602_ll_output copies the frame into a firmware TX block, so the
         * descriptor can be released immediately. is_sta = 1 (STA-only). */
        if( bl602_ll_output( 1, pxNetworkBuffer->pucEthernetBuffer,
                             ( uint16_t ) pxNetworkBuffer->xDataLength, NULL ) == 0 )
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

NetworkInterface_t * pxBl602_FillInterfaceDescriptor( BaseType_t xEMACIndex,
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

/* ===== One-shot bring-up (called once the WiFi STA driver is up) =========== */
void bl602_tcp_bringup( void )
{
    static NetworkInterface_t xInterface;
    static NetworkEndPoint_t  xEndPoint;
    static BaseType_t         xDone = pdFALSE;
    uint8_t ucMAC[ 6 ] = { 0 };
    static const uint8_t ucZero[ 4 ] = { 0, 0, 0, 0 };

    if( xDone != pdFALSE )
    {
        return;
    }
    xDone = pdTRUE;

    ( void ) wifi_mgmr_sta_mac_get( ucMAC );

    pxBl602_FillInterfaceDescriptor( 0, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucZero, ucZero, ucZero, ucZero, ucMAC );
    xEndPoint.bits.bWantDHCP = pdTRUE_UNSIGNED;

    FreeRTOS_IPInit_Multi();
    bl602_freertos_tcp_set_ready();
}

/* Re-kick DHCP — called from the driver's connect indication (bl_rx.c) once the STA
 * associates, so the stack (re)starts DHCP promptly instead of waiting on the poll. */
void bl602_tcp_kick( void )
{
    if( pxMyInterface != NULL )
    {
        FreeRTOS_NetworkDown( pxMyInterface );
    }
}

/* Live IPv4 config from the STA endpoint (was the lwIP netif). All network-order. */
int bl602_tcp_ip4_get( uint32_t * addr, uint32_t * mask, uint32_t * gw, uint32_t * dns )
{
    NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );

    if( addr != NULL ) { *addr = ( px != NULL ) ? px->ipv4_settings.ulIPAddress      : 0U; }
    if( mask != NULL ) { *mask = ( px != NULL ) ? px->ipv4_settings.ulNetMask        : 0U; }
    if( gw   != NULL ) { *gw   = ( px != NULL ) ? px->ipv4_settings.ulGatewayAddress : 0U; }
    if( dns  != NULL ) { *dns  = ( px != NULL ) ? px->ipv4_settings.ulDNSServerAddresses[ 0 ] : 0U; }
    return 0;
}

/* ===== stack hooks ========================================================= */
/* Random for the DHCP xid / TCP ISN: the BL602 hardware TRNG (bflb_sec_trng,
 * same engine the mbedTLS entropy port uses). bflb_trng_readlen returns 0 on OK. */
BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber )
{
    return ( bflb_trng_readlen( ( uint8_t * ) pulNumber, sizeof( *pulNumber ) ) == 0 ) ? pdTRUE : pdFALSE;
}

uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress, uint16_t usSourcePort,
                                             uint32_t ulDestinationAddress, uint16_t usDestinationPort )
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
        ( void ) wifi_mgmr_sta_mac_get( mac );
        ( void ) snprintf( host, sizeof( host ), "bl602_%02x%02x%02x", mac[ 3 ], mac[ 4 ], mac[ 5 ] );
    }
    return host;
}

BaseType_t xApplicationDNSQueryHook( const char * pcName )
{
    const char * h  = pcApplicationHostnameHook();
    size_t       hl = strlen( h );
    if( strcasecmp( pcName, h ) == 0 ) { return pdPASS; }
    if( ( strncasecmp( pcName, h, hl ) == 0 ) && ( strcasecmp( &pcName[ hl ], ".local" ) == 0 ) ) { return pdPASS; }
    return pdFAIL;
}

void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    if( eNetworkEvent == eNetworkUp )
    {
        NetworkEndPoint_t * px = FreeRTOS_FirstEndPoint( NULL );
        uint32_t ulIP = ( px != NULL ) ? px->ipv4_settings.ulIPAddress : 0U;
        printf( "[TCP] network up: %u.%u.%u.%u\r\n",
                ( unsigned ) ( ulIP & 0xffU ), ( unsigned ) ( ( ulIP >> 8 ) & 0xffU ),
                ( unsigned ) ( ( ulIP >> 16 ) & 0xffU ), ( unsigned ) ( ( ulIP >> 24 ) & 0xffU ) );
        /* Advance the wifi_mgmr state machine out of wifiConnected_ipObtaining:
         * with FreeRTOS-Plus-TCP owning DHCP, the mgmr never sees lwIP's got-IP
         * callback, so without this it sits in ipObtaining, its timer fires and
         * it self-disconnects (802.11 reason 3) a few seconds after DHCP. */
        {
            extern int wifi_mgmr_api_ip_got( void );
            wifi_mgmr_api_ip_got();
        }
        /* Tell the app (wifi_event_handler) an IP is up. */
        platform_post_event( EV_WIFI, CODE_WIFI_ON_GOT_IP, 0 );
    }
    else
    {
        printf( "[TCP] network down\r\n" );
    }
}
