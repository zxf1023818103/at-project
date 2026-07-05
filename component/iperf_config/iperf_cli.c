/*
 * iperf_cli.c — the `iperf` console command wrapping the throughput probes (component/iperf_config).
 * On-demand alternative to AmebaD's auto-run-on-network-up, for the shared FreeRTOS-Plus-CLI console.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "iperf.h"

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
        iperf_client_start();
        snprintf( out, outlen, "iperf: TCP TX to the built-in target started\r\n" );
    }
    else if( plen == 3 && strncmp( p, "udp", 3 ) == 0 )
    {
        iperf_udp_server_start();
        snprintf( out, outlen, "iperf: UDP RX sink started on :5001\r\n" );
    }
    else if( plen == 5 && strncmp( p, "udptx", 5 ) == 0 )
    {
        iperf_udp_client_start();
        snprintf( out, outlen, "iperf: UDP TX to the built-in target started\r\n" );
    }
    else
    {
        snprintf( out, outlen, "usage: iperf [server|tx|udp|udptx]\r\n" );
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
