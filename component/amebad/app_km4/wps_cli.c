/*
 * wps_cli.c - on-device WPS (WiFi Protected Setup) connect test (make WPS=ON).
 *
 * References the SDK's cmd_wps (wpa_supplicant/wifi_wps_config.c): it drives wps_start(), which runs
 * the WPS scan + EAP-WSC negotiation against a WPS-enabled AP and, on success, connects + stores the
 * learned credential. wps_start() blocks (crypto-heavy), so we run it on a dedicated task and let the
 * console return immediately - the result lands in the [WPS] log.
 *
 *   wps pbc            - push-button: trigger within 2 min of pressing/enabling WPS-PBC on the AP
 *   wps pin            - display:     prints an 8-digit PIN to enter on the AP
 *   wps pin <8-digit>  - keypad:      use the AP's PIN
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"
#include "wifi/wifi_conf.h"    /* Realtek u8/u16 types + WiFi API - before the supplicant headers */
#include "wps_defs.h"          /* WPS_CONFIG_PUSHBUTTON / _DISPLAY / _KEYPAD (src/wps) */
#include "wifi_wps_config.h"   /* wps_start / wps_generate_pin / cmd_wps */

static char           s_wps_pin[16];
static unsigned short s_wps_cfg;

static void wps_thread( void * param )
{
    ( void ) param;
    int rc = wps_start( s_wps_cfg, ( s_wps_pin[ 0 ] != '\0' ) ? s_wps_pin : NULL, 0, NULL );
    log_info( "[WPS] wps_start rc=%d -> %s", rc, ( rc == 0 ) ? "CONNECTED" : "failed/timeout" );
    vTaskDelete( NULL );
}

static BaseType_t cmd_wps_run( char * out, size_t outlen, const char * cmd )
{
    BaseType_t   plen = 0;
    const char * p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( p == NULL )
    {
        snprintf( out, outlen, "usage: wps pbc | wps pin [<8-digit pin>]\r\n" );
        return pdFALSE;
    }

    s_wps_pin[ 0 ] = '\0';

    if( strncmp( p, "pbc", 3 ) == 0 )
    {
        s_wps_cfg = WPS_CONFIG_PUSHBUTTON;
        log_info( "[WPS] PBC: enable/press WPS-PBC on the AP within 2 min" );
        snprintf( out, outlen, "wps pbc started -> see [WPS] log\r\n" );
    }
    else if( strncmp( p, "pin", 3 ) == 0 )
    {
        BaseType_t   pl  = 0;
        const char * pin = FreeRTOS_CLIGetParameter( cmd, 2, &pl );

        if( pin != NULL )
        {
            snprintf( s_wps_pin, sizeof( s_wps_pin ), "%.*s", ( int ) pl, pin );
            s_wps_cfg = WPS_CONFIG_KEYPAD;
            log_info( "[WPS] PIN keypad: using AP pin %s", s_wps_pin );
            snprintf( out, outlen, "wps pin keypad (%s) started\r\n", s_wps_pin );
        }
        else
        {
            snprintf( s_wps_pin, sizeof( s_wps_pin ), "%08u", wps_generate_pin() );
            s_wps_cfg = WPS_CONFIG_DISPLAY;
            log_info( "[WPS] PIN display: enter %s on the AP within 2 min", s_wps_pin );
            snprintf( out, outlen, "wps pin display: PIN=%s\r\n", s_wps_pin );
        }
    }
    else
    {
        snprintf( out, outlen, "usage: wps pbc | wps pin [<8-digit pin>]\r\n" );
        return pdFALSE;
    }

    if( xTaskCreate( wps_thread, "wps", 2048, NULL, tskIDLE_PRIORITY + 3, NULL ) != pdPASS )
        snprintf( out, outlen, "wps: xTaskCreate failed\r\n" );

    return pdFALSE;
}

void wps_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_wps_def = {
        "wps",
        "wps:\r\n wps pbc | wps pin [<pin>] - connect via WiFi Protected Setup\r\n",
        cmd_wps_run, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_wps_def );
}
