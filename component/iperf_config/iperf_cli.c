/*
 * iperf_cli.c — the `iperf` console command wrapping the throughput probes (component/iperf_config).
 * On-demand alternative to AmebaD's auto-run-on-network-up, for the shared FreeRTOS-Plus-CLI console.
 */
#include <stdio.h>
#include <stdlib.h>   /* setenv */
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"   /* FreeRTOS_inet_ntoa */
#include "syscalls.h"           /* env_save */

#include "iperf.h"

/* `iperf tx|udptx [ip]`: an explicit address is stashed in IPERF_HOST, which is what
 * iperf_target_ip() reads. Persisting it in the env means the next run needs no argument
 * and the target survives a reboot -- the built-in default only matches one bench. */
static void prvSetTargetFromArg( const char *cmd )
{
    BaseType_t   xLen = 0;
    const char * pcIp = FreeRTOS_CLIGetParameter( cmd, 2, &xLen );

    if( ( pcIp != NULL ) && ( xLen > 0 ) && ( xLen < 16 ) )
    {
        char cBuf[ 16 ];

        ( void ) memcpy( cBuf, pcIp, ( size_t ) xLen );
        cBuf[ xLen ] = '\0';
        ( void ) setenv( "IPERF_HOST", cBuf, 1 );
        env_save( "IPERF_HOST" );
    }
}

static void prvReportTarget( char *out, size_t outlen, const char *pcWhat )
{
    char cAddr[ 16 ];

    FreeRTOS_inet_ntoa( iperf_target_ip(), cAddr );
    ( void ) snprintf( out, outlen, "iperf: %s TX to %s:5001 started (host: iperf -s%s)\r\n",
                       pcWhat, cAddr, ( pcWhat[ 0 ] == 'U' ) ? " -u" : "" );
}

static BaseType_t cmd_iperf( char *out, size_t outlen, const char *cmd )
{
    BaseType_t plen = 0;
    const char *p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( p == NULL || ( plen == 6 && strncmp( p, "server", 6 ) == 0 ) )
    {
        iperf_server_start();
        snprintf( out, outlen, "iperf: TCP RX sink started on :5001 (host: iperf -c <dev-ip> -p 5001 -t 15)\r\n" );
    }
    else if( plen == 2 && strncmp( p, "tx", 2 ) == 0 )
    {
        prvSetTargetFromArg( cmd );
        iperf_client_start();
        prvReportTarget( out, outlen, "TCP" );
    }
    else if( plen == 3 && strncmp( p, "udp", 3 ) == 0 )
    {
        iperf_udp_server_start();
        snprintf( out, outlen, "iperf: UDP RX sink started on :5001\r\n" );
    }
    else if( plen == 5 && strncmp( p, "udptx", 5 ) == 0 )
    {
        prvSetTargetFromArg( cmd );
        iperf_udp_client_start();
        prvReportTarget( out, outlen, "UDP" );
    }
    else
    {
        snprintf( out, outlen, "usage: iperf [server|tx [ip]|udp|udptx [ip]]\r\n" );
    }
    return pdFALSE;
}

static const CLI_Command_Definition_t kIperfCmd = {
    "iperf",
    "iperf [server|tx|udp|udptx]:\r\n throughput test (server=TCP RX sink, tx=TCP TX, udp=UDP RX, udptx=UDP TX)\r\n",
    cmd_iperf,
    -1
};

void iperf_cli_register( void )
{
    FreeRTOS_CLIRegisterCommand( &kIperfCmd );
}
