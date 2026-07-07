/*
 * net_cli.c - cross-platform network-diagnostic console commands.
 *
 *   resolve <host> - DNS lookup. Uses c-ares (component/c-ares_config) under CARES=ON, else the
 *                    FreeRTOS-Plus-TCP resolver (FreeRTOS_getaddrinfo) so it works on any image.
 *   ip6            - list every IPv6 address the stack holds (link-local + SLAAC global from the RA).
 *
 * Extracted from AmebaD's project/amebad/app_km4/wifi_cli.c so bl602 shares the same commands; the
 * AmebaD-specific `iwpriv` (RTK driver ioctl) stays there. Uses printf for the multi-line ip6 output
 * (routes to each platform's console) to stay dependency-light.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#if ( ipconfigUSE_IPv6 != 0 )
    #include "FreeRTOS_IPv6_Sockets.h"   /* FreeRTOS_inet_ntop6 */
#endif
#ifdef AT_PROJECT_CARES
    #include "cares_resolve.h"
#endif

#include "net_cli.h"

/* Resolve `host` for `family` (FREERTOS_AF_INET / _INET6) into `fa`. c-ares under CARES=ON, else the
 * FreeRTOS-Plus-TCP resolver (same idiom as network_transport/tcp_sockets_wrapper.c). 0 on success. */
static int prv_dns_one( const char * host, int family, struct freertos_sockaddr * fa )
{
#ifdef AT_PROJECT_CARES
    return cares_resolve( host, family, fa );
#else
    struct freertos_addrinfo   hints;
    struct freertos_addrinfo * res = NULL;
    int                        rc = -1;

    ( void ) memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = family;
    if( ( FreeRTOS_getaddrinfo( host, NULL, &hints, &res ) == 0 ) && ( res != NULL ) && ( res->ai_addr != NULL ) )
    {
        ( void ) memcpy( fa, res->ai_addr, sizeof( *fa ) );
        /* FreeRTOS_getaddrinfo leaves the embedded sockaddr's sin_family at 0; carry the queried one. */
        fa->sin_family = ( uint8_t ) family;
        rc = 0;
    }
    if( res != NULL ) { FreeRTOS_freeaddrinfo( res ); }
    return rc;
#endif
}

/* resolve <host> : print the first A (IPv4) or, failing that, AAAA (IPv6) record. */
static BaseType_t cmd_resolve( char * out, size_t outlen, const char * cmd )
{
    BaseType_t               plen = 0;
    const char *             host = FreeRTOS_CLIGetParameter( cmd, 1, &plen );
    char                     hbuf[ 80 ];
    struct freertos_sockaddr fa;

    if( ( host == NULL ) || ( plen == 0 ) )
    {
        snprintf( out, outlen, "usage: resolve <hostname>\r\n" );
        return pdFALSE;
    }
    if( plen >= ( BaseType_t ) sizeof( hbuf ) ) { plen = ( BaseType_t ) sizeof( hbuf ) - 1; }
    memcpy( hbuf, host, ( size_t ) plen );
    hbuf[ plen ] = '\0';

    if( prv_dns_one( hbuf, FREERTOS_AF_INET, &fa ) == 0 )
    {
        uint32_t ip = fa.sin_address.ulIP_IPv4;
        snprintf( out, outlen, "resolve %s -> %u.%u.%u.%u  (A)\r\n", hbuf,
                  ( unsigned ) ( ip & 0xffU ),           ( unsigned ) ( ( ip >> 8 ) & 0xffU ),
                  ( unsigned ) ( ( ip >> 16 ) & 0xffU ), ( unsigned ) ( ( ip >> 24 ) & 0xffU ) );
    }
#if ( ipconfigUSE_IPv6 != 0 )
    else if( prv_dns_one( hbuf, FREERTOS_AF_INET6, &fa ) == 0 )
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
    return pdFALSE;
}

#if ( ipconfigUSE_IPv6 != 0 )
/* ip6 : list every IPv6 address the stack holds (link-local fe80:: + any SLAAC global from the RA). */
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
            printf( "[IPv6] addr %s/%u%s\r\n", buf, ( unsigned ) pxEP->ipv6_settings.uxPrefixLength,
                    ( pxEP->bits.bWantRA != pdFALSE_UNSIGNED ) ? " (SLAAC)" : " (link-local)" );
            n++;
        }
    }
    snprintf( out, outlen, "ip6: %d IPv6 address(es) -> see [IPv6] log\r\n", n );
    return pdFALSE;
}
#endif

void net_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_resolve_def = {
        "resolve",
        "resolve <host>:\r\n look a hostname up (c-ares under CARES=ON, else the FreeRTOS+TCP resolver)\r\n",
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
