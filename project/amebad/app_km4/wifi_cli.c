/*
 * wifi_cli.c - AmebaD-specific WiFi driver console command.
 *
 *   iwpriv [cmd] - Realtek wlan-driver private ioctl passthrough (no bl602 equivalent).
 *
 * The cross-platform `resolve` + `ip6` commands moved to component/net_cli_config (net_cli_register);
 * the `wifi` forget/drop command is in component/wifi_service_config (wifi_cli_register). main()'s
 * app_console_register_platform() calls all three.
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

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

/* Registers only the AmebaD-specific `iwpriv` driver command. The shared `resolve`/`ip6` come from
 * net_cli_register() (component/net_cli_config); main() calls both. */
void amebad_wifi_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_iwpriv_def = {
        "iwpriv",
        "iwpriv [cmd]:\r\n run a wlan driver private command (default read_mac -> WiFi MAC)\r\n",
        cmd_iwpriv, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_iwpriv_def );
}
