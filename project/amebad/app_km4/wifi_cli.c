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
#ifdef AT_PROJECT_CARES
#include "cares_resolve.h"
#endif

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

/* resolve <host> : look the name up through c-ares (uses the DNS servers from
 * cares_resolver_set_servers -- e.g. `set DNS_SERVER 2400:3200::1` for IPv6 AliDNS). */
static BaseType_t cmd_resolve( char * out, size_t outlen, const char * cmd )
{
    BaseType_t   plen = 0;
    const char * host = FreeRTOS_CLIGetParameter( cmd, 1, &plen );
#ifdef AT_PROJECT_CARES
    struct freertos_sockaddr fa;
    char                     hbuf[ 80 ];

    if( ( host == NULL ) || ( plen == 0 ) )
    {
        snprintf( out, outlen, "usage: resolve <hostname>\r\n" );
        return pdFALSE;
    }
    if( plen >= ( BaseType_t ) sizeof( hbuf ) ) { plen = ( BaseType_t ) sizeof( hbuf ) - 1; }
    memcpy( hbuf, host, ( size_t ) plen );
    hbuf[ plen ] = '\0';

    if( cares_resolve( hbuf, FREERTOS_AF_INET, &fa ) == 0 )
    {
        uint32_t ip = fa.sin_address.ulIP_IPv4;
        snprintf( out, outlen, "resolve %s -> %u.%u.%u.%u  (A)\r\n", hbuf,
                  ( unsigned ) ( ip & 0xffU ),          ( unsigned ) ( ( ip >> 8 ) & 0xffU ),
                  ( unsigned ) ( ( ip >> 16 ) & 0xffU ), ( unsigned ) ( ( ip >> 24 ) & 0xffU ) );
    }
#if ( ipconfigUSE_IPv6 != 0 )
    else if( cares_resolve( hbuf, FREERTOS_AF_INET6, &fa ) == 0 )
    {
        char ip6[ 48 ];
        FreeRTOS_inet_ntop6( fa.sin_address.xIP_IPv6.ucBytes, ip6, ( socklen_t ) sizeof( ip6 ) );
        snprintf( out, outlen, "resolve %s -> %s  (AAAA)\r\n", hbuf, ip6 );
    }
#endif
    else
    {
        snprintf( out, outlen, "resolve %s -> FAILED\r\n", hbuf );
    }
#else
    ( void ) host; ( void ) plen;
    snprintf( out, outlen, "resolve: c-ares not built (make CARES=ON)\r\n" );
#endif
    return pdFALSE;
}

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

/* Realtek driver private-ioctl passthrough (the SDK's iwpriv mechanism): sends a private
 * command string to the wlan driver via rltk_wlan_control(SIOCDEVPRIVATE). Declared here
 * (avoid pulling rtw headers); same call wifi_get_mac_address() uses under the hood. */
extern int wext_private_command_with_retval( const char * ifname, char * cmd, char * ret_buf, int ret_len );

/* `iwpriv [cmd]`: run a driver private command and print its return string. With no
 * argument defaults to `read_mac` (the WiFi MAC). Examples: `iwpriv`, `iwpriv read_mac`. */
static BaseType_t cmd_iwpriv( char * out, size_t outlen, const char * cmd )
{
    char priv[ 64 ];
    char ret[ 64 ];
    const char * args = cmd;
    int rc;

    /* skip the command word ("iwpriv") and the following spaces */
    while( ( *args != '\0' ) && ( *args != ' ' ) ) { args++; }
    while( *args == ' ' ) { args++; }

    strncpy( priv, ( *args != '\0' ) ? args : "read_mac", sizeof( priv ) - 1 );
    priv[ sizeof( priv ) - 1 ] = '\0';

    memset( ret, 0, sizeof( ret ) );
    rc = wext_private_command_with_retval( "wlan0", priv, ret, sizeof( ret ) );
    if( rc < 0 )
    {
        snprintf( out, outlen, "iwpriv '%s' failed (%d) - is WiFi up?\r\n", priv, rc );
    }
    else
    {
        snprintf( out, outlen, "iwpriv %s: %s\r\n", priv, ret );
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

    static const CLI_Command_Definition_t cmd_iwpriv_def = {
        "iwpriv",
        "iwpriv [cmd]:\r\n run a wlan driver private command (default read_mac -> WiFi MAC)\r\n",
        cmd_iwpriv, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_iwpriv_def );

    static const CLI_Command_Definition_t cmd_resolve_def = {
        "resolve",
        "resolve <host>:\r\n look a hostname up via c-ares (servers from DNS_SERVER env / DHCP)\r\n",
        cmd_resolve, 1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_resolve_def );

#if ( ipconfigUSE_IPv6 != 0 )
    static const CLI_Command_Definition_t cmd_ip6_def = {
        "ip6",
        "ip6:\r\n list the device's IPv6 addresses (link-local + SLAAC)\r\n",
        cmd_ip6, 0
    };
    FreeRTOS_CLIRegisterCommand( &cmd_ip6_def );
#endif
}
