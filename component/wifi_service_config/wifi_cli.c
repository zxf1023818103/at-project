/*
 * wifi_cli.c — the shared `wifi` console command over the wifi_service policy.
 *   wifi forget - disconnect, stop reconnecting, erase the saved WIFI_* credentials
 *   wifi drop   - drop the link (the reconnect monitor re-associates) -- a self-test of the monitor
 * Platform-agnostic: forget/reconnect live in wifi_service, disconnect in the wifi_port HAL.
 */
#include "wifi_service.h"
#include "wifi_port.h"
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include <string.h>
#include <stdio.h>

static BaseType_t cmd_wifi( char * out, size_t outlen, const char * cmd )
{
    BaseType_t   plen = 0;
    const char * p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( ( p != NULL ) && ( plen == 6 ) && ( strncmp( p, "forget", 6 ) == 0 ) )
    {
        wifi_forget();
        ( void ) snprintf( out, outlen, "wifi forget: disconnected, reconnect disabled, credentials erased\r\n" );
    }
    else if( ( p != NULL ) && ( plen == 4 ) && ( strncmp( p, "drop", 4 ) == 0 ) )
    {
        wifi_port_disconnect();
        ( void ) snprintf( out, outlen, "wifi drop: link dropped (the reconnect monitor will re-associate)\r\n" );
    }
    else
    {
        ( void ) snprintf( out, outlen, "usage: wifi forget | wifi drop\r\n" );
    }

    return pdFALSE;
}

void wifi_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_wifi_def = {
        "wifi",
        "wifi:\r\n"
        " wifi forget - disconnect, stop reconnect, erase saved WiFi credentials\r\n"
        " wifi drop   - drop the link; the monitor re-associates (self-test)\r\n",
        cmd_wifi, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_wifi_def );
}
