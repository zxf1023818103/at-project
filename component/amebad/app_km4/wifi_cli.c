/*
 * wifi_cli.c - WiFi management console command.
 *
 *   wifi forget - disconnect the current AP, permanently stop the reconnect monitor from
 *                 re-associating, and erase the saved credentials (WIFI_SSID/PASSWORD/CHANNEL/
 *                 SECURITY) from the KV store. The device stays offline until the credentials
 *                 are set again (e.g. `wps pbc`, `setenv WIFI_SSID ...`, or a reboot defaults).
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#if ( ipconfigUSE_IPv6 != 0 )
    #include "FreeRTOS_IPv6_Sockets.h"   /* FreeRTOS_inet_ntop6 */
#endif
#include "at_log.h"

extern void wifi_forget( void );   /* main.c */

#if ( ipconfigUSE_IPv6 != 0 )
/* `ip6`: list every IPv6 address the stack holds (link-local fe80:: + any SLAAC global from the RA). */
static BaseType_t cmd_ip6( char * out, size_t outlen, const char * cmd )
{
    NetworkEndPoint_t * pxEP;
    char                buf[ 48 ];
    int                 n = 0;

    ( void ) cmd;
    for( pxEP = FreeRTOS_FirstEndPoint( NULL ); pxEP != NULL; pxEP = FreeRTOS_NextEndPoint( NULL, pxEP ) )
    {
        if( pxEP->bits.bIPv6 != pdFALSE_UNSIGNED )
        {
            FreeRTOS_inet_ntop6( pxEP->ipv6_settings.xIPAddress.ucBytes, buf, ( socklen_t ) sizeof( buf ) );
            log_info( "[IPv6] addr %s/%u%s", buf, ( unsigned ) pxEP->ipv6_settings.uxPrefixLength,
                      ( pxEP->bits.bWantRA != pdFALSE_UNSIGNED ) ? " (SLAAC)" : " (link-local)" );
            n++;
        }
    }
    snprintf( out, outlen, "ip6: %d IPv6 address(es) -> see [IPv6] log\r\n", n );
    return pdFALSE;
}
#endif

static BaseType_t cmd_wifi( char * out, size_t outlen, const char * cmd )
{
    BaseType_t   plen = 0;
    const char * p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( ( p != NULL ) && ( plen == 6 ) && ( strncmp( p, "forget", 6 ) == 0 ) )
    {
        wifi_forget();
        snprintf( out, outlen, "wifi forget: disconnected, reconnect disabled, credentials erased\r\n" );
    }
    else
    {
        snprintf( out, outlen, "usage: wifi forget\r\n" );
    }

    return pdFALSE;
}

void wifi_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_wifi_def = {
        "wifi",
        "wifi:\r\n wifi forget - disconnect, stop reconnect, erase saved WiFi credentials\r\n",
        cmd_wifi, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_wifi_def );

#if ( ipconfigUSE_IPv6 != 0 )
    static const CLI_Command_Definition_t cmd_ip6_def = {
        "ip6",
        "ip6:\r\n list the device's IPv6 addresses (link-local + SLAAC)\r\n",
        cmd_ip6, 0
    };
    FreeRTOS_CLIRegisterCommand( &cmd_ip6_def );
#endif
}
