/*
 * monitor_bflb.c — the Bouffalo (BL602 / BL616 / BL616CL) platform port for the shared
 * console engine (component/monitor_config). Single application core, so there is no
 * dual-core hand-off: this core owns the console for the whole run. RX polls at_console_getc
 * (bflb_uart_getchar is non-blocking) with a short vTaskDelay when idle; output is via the
 * engine's at_console_write_raw. The console runs on uart0 (the log/console port); the AT
 * command protocol used to keep uart1 (the deleted C++ AT framework) — separate peripherals, no RX contention.
 */
#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "console.h"     /* at_console_getc — poll the active console backend (uart0) */
#include "monitor.h"     /* the shared engine seam */

extern int bl_sys_reset_por(void);   /* bflb_sys: full power-on reset (resolves at image link) */

/* This core's identity for the prompt (passed by CMake per chip; e.g. "bl602"). */
#ifndef MONITOR_CORE_NAME
#define MONITOR_CORE_NAME "bouffalo"
#endif

static int s_started;

const char *monitor_core_name( void )
{
    return MONITOR_CORE_NAME;
}

void monitor_port_init( void )
{
    s_started = 0;
}

int monitor_rx_get( uint8_t *c )
{
    int b;

    if( !s_started )            /* single owner: signal "ownership gained" once so the banner prints */
    {
        s_started = 1;
        return -1;
    }
    b = at_console_getc();       /* non-blocking (bflb_uart_getchar) */
    if( b < 0 )
    {
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
        return 0;
    }
    *c = ( uint8_t ) b;
    return 1;
}

int monitor_after_command( void )
{
    return 0;                    /* single core: no console hand-off */
}

/* ---- reboot command ------------------------------------------------------ */

static BaseType_t cmd_reboot( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    snprintf( out, outlen, "rebooting ...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );   /* let the line flush before the reset fires */
    bl_sys_reset_por();
    return pdFALSE;                      /* not reached */
}

static const CLI_Command_Definition_t kBflbCmds[] = {
    { "reboot", "reboot:\r\n reset the chip\r\n", cmd_reboot, 0 },
};

/* Called from main() after monitor_start() (the register-after-start pattern). */
void bouffalo_monitor_cmds_register( void )
{
    unsigned i;
    for( i = 0; i < sizeof( kBflbCmds ) / sizeof( kBflbCmds[ 0 ] ); i++ )
    {
        FreeRTOS_CLIRegisterCommand( &kBflbCmds[ i ] );
    }
}
