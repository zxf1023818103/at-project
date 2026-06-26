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

extern void wifi_forget( void );   /* main.c */

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
}
