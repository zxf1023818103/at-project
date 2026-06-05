/*
 * AmebaD KM4 (HP) image2 — WiFi + FreeRTOS-Plus-TCP DHCP bring-up.
 *
 * Goal of this slice: associate to an AP (WPA2-PSK) and obtain an IP via
 * FreeRTOS-Plus-TCP's DHCP client, proving the FreeRTOS+TCP stack runs on
 * AmebaD over the Realtek WiFi driver (replacing lwip's IP role). Sockets /
 * TCP / apps are follow-on.
 *
 * Flow (in wifi_bringup_thread):
 *   wifi_on(STA) -> wifi_connect(AIoT) -> get MAC -> register netif with driver
 *   -> FreeRTOS+TCP: fill interface + endpoint(DHCP) -> FreeRTOS_IPInit_Multi
 *   -> DHCP -> vApplicationIPNetworkEventHook(eNetworkUp) prints the IP.
 */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wifi_conf.h"            /* wifi_on / wifi_connect / wifi_get_mac_address */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"     /* NetworkInterface_t / NetworkEndPoint_t / FillEndPoint */

/* basic AT log / shell console (monitor shell over the log UART) */
extern void shell_init_rom(u32 TBLSz, void *pTBL);
extern void shell_init_ram(void);
extern void shell_uart_irq_rom(void *Data);

/* FreeRTOS+TCP Realtek glue (component/amebad/freertos_tcp) */
extern NetworkInterface_t *pxRealtek_FillInterfaceDescriptor(BaseType_t xEMACIndex,
                                                             NetworkInterface_t *pxInterface);
extern void amebad_register_wlan_netif(int idx, unsigned char *mac);
extern void amebad_freertos_tcp_set_ready(void);
extern void iperf_client_start(void);

#define WLAN_STA_IDX  0

static NetworkInterface_t xInterface;
static NetworkEndPoint_t  xEndPoint;

/* DHCP-only: start with 0.0.0.0 so the endpoint requests an address. */
static const uint8_t ucIPAddress[ 4 ]    = { 0, 0, 0, 0 };
static const uint8_t ucNetMask[ 4 ]      = { 255, 255, 255, 0 };
static const uint8_t ucGatewayAddress[4] = { 0, 0, 0, 0 };
static const uint8_t ucDNSServer[ 4 ]    = { 0, 0, 0, 0 };

/* Called by the IP task when the interface comes up (DHCP done) or goes down.
 * ipconfigIPv4_BACKWARD_COMPATIBLE=1 -> single-arg hook. */
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
{
    if( eNetworkEvent == eNetworkUp )
    {
        uint32_t ulIP = 0, ulMask = 0, ulGW = 0, ulDNS = 0;
        FreeRTOS_GetAddressConfiguration( &ulIP, &ulMask, &ulGW, &ulDNS );
        /* IPs are in network byte order; byte 0 = first octet. */
        DiagPrintf( "[KM4] ==== network UP ====\n\r" );
        DiagPrintf( "[KM4] DHCP IP   %d.%d.%d.%d\n\r",
                    (int)( ulIP & 0xff ), (int)( ( ulIP >> 8 ) & 0xff ),
                    (int)( ( ulIP >> 16 ) & 0xff ), (int)( ( ulIP >> 24 ) & 0xff ) );
        DiagPrintf( "[KM4] netmask   %d.%d.%d.%d\n\r",
                    (int)( ulMask & 0xff ), (int)( ( ulMask >> 8 ) & 0xff ),
                    (int)( ( ulMask >> 16 ) & 0xff ), (int)( ( ulMask >> 24 ) & 0xff ) );
        DiagPrintf( "[KM4] gateway   %d.%d.%d.%d\n\r",
                    (int)( ulGW & 0xff ), (int)( ( ulGW >> 8 ) & 0xff ),
                    (int)( ( ulGW >> 16 ) & 0xff ), (int)( ( ulGW >> 24 ) & 0xff ) );
        iperf_client_start();   /* kick the TX-throughput test against the WSL iperf -s */
    }
    else
    {
        DiagPrintf( "[KM4] ==== network DOWN ====\n\r" );
    }
}

/* "ab" -> 0xAB ; tolerant of upper/lower case. */
static unsigned char prvHexByte( const char * p )
{
    unsigned char v = 0;
    int i;
    for( i = 0; i < 2; i++ )
    {
        char c = p[ i ];
        v <<= 4;
        if( ( c >= '0' ) && ( c <= '9' ) ) v |= ( unsigned char )( c - '0' );
        else if( ( c >= 'a' ) && ( c <= 'f' ) ) v |= ( unsigned char )( c - 'a' + 10 );
        else if( ( c >= 'A' ) && ( c <= 'F' ) ) v |= ( unsigned char )( c - 'A' + 10 );
    }
    return v;
}

static void wifi_bringup_thread( void *param )
{
    char mac_str[ 20 ] = { 0 };   /* wifi_get_mac_address fills "xx:xx:xx:xx:xx:xx" */
    unsigned char mac[ 6 ] = { 0 };
    int i;
    ( void ) param;

    DiagPrintf( "[KM4] wifi_on(RTW_MODE_STA)...\n\r" );
    if( wifi_on( RTW_MODE_STA ) < 0 )
    {
        DiagPrintf( "[KM4] wifi_on FAILED\n\r" );
        vTaskDelete( NULL );
        return;
    }

    /* Bring up FreeRTOS+TCP BEFORE associating: the driver starts delivering RX
     * (beacons/probes) during association, and the RX bridge needs the buffer
     * pool + IP task to already exist. After wifi_on the driver is running, so
     * the MAC is valid and the netif can be registered. */
    wifi_get_mac_address( mac_str );          /* returns the STRING "xx:xx:.." */
    for( i = 0; i < 6; i++ )                   /* parse to 6 binary bytes */
    {
        mac[ i ] = prvHexByte( &mac_str[ i * 3 ] );
    }
    DiagPrintf( "[KM4] MAC %s -> %02x:%02x:%02x:%02x:%02x:%02x\n\r",
                mac_str, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    /* Register netif + MAC with the driver (so RX delivers and LwIP_GetMAC
     * returns the real MAC), then bring up FreeRTOS+TCP BEFORE associating. */
    amebad_register_wlan_netif( WLAN_STA_IDX, mac );
    pxRealtek_FillInterfaceDescriptor( WLAN_STA_IDX, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucIPAddress, ucNetMask,
                           ucGatewayAddress, ucDNSServer, mac );
    xEndPoint.bits.bWantDHCP = pdTRUE;
    DiagPrintf( "[KM4] FreeRTOS_IPInit_Multi...\n\r" );
    FreeRTOS_IPInit_Multi();
    amebad_freertos_tcp_set_ready();

    DiagPrintf( "[KM4] wifi_connect(AIoT)...\n\r" );
    if( wifi_connect( "AIoT", RTW_SECURITY_WPA2_AES_PSK, "123456789",
                      4, 9, 0, NULL ) != RTW_SUCCESS )
    {
        DiagPrintf( "[KM4] wifi_connect FAILED\n\r" );
        vTaskDelete( NULL );
        return;
    }
    DiagPrintf( "[KM4] associated; waiting for DHCP...\n\r" );

    vTaskDelete( NULL );
}

int main(void)
{
    DiagPrintf("\n\r[KM4] at-project image2 — FreeRTOS+TCP DHCP bring-up\n\r");
    DiagPrintf("[KM4] FreeRTOS kernel %s\n\r", tskKERNEL_VERSION_NUMBER);

    /* AT log / shell console (ROM + RAM shell + LOGUART RX ISR). */
    shell_init_rom(0, 0);
    shell_init_ram();
    InterruptRegister((IRQ_FUN)shell_uart_irq_rom, UART_LOG_IRQ, (u32)NULL, 5);
    InterruptEn(UART_LOG_IRQ, 5);

    /* WiFi+DHCP bring-up runs as its own task (wifi_connect blocks). */
    if (xTaskCreate(wifi_bringup_thread, "wifi_bringup", (512 + 768), NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS)
        DiagPrintf("[KM4] xTaskCreate(wifi_bringup) failed\n\r");

    vTaskStartScheduler();

    for (;;) {
    }
}
