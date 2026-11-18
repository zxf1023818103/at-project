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
#include <string.h>

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

#if defined( BL616 ) || defined( BL616CL )
/* `reboot uartburn` (mirrors the AmebaD command): force the boot ROM into UART ISP so the
 * host can flash over the console UART with no BOOT-pin press. HBN_RSV2 (0x2000F108) is in
 * the always-on domain: RELEASE_CORE=4 (the ROM's "honour user_boot_sel" flag) | USER_BOOT_SEL=1
 * (ISP) = 0x44000000; the ROM consumes it on the next boot. The reset MUST be
 * GLB_SW_System_Reset (SYS_RESET keeps the HBN domain) — bl_sys_reset_por is a power-on reset
 * that would wipe the flag. Field layout per bl616_hbn.h; values per OpenOCD's bl616 support. */
#define BL616_HBN_RSV2         ( *( volatile uint32_t * ) 0x2000F108u )
#define BL616_BOOT_SEL_ISP     0x44000000u
/* Reset via bl_sys_reset_por (GLB software power-on reset): it fully resets the clock/PLL
 * domains so the boot ROM's ISP brings its PLL up from a clean state ("SET CLOCK PLL FAIL"
 * otherwise, HW-caught when entering ISP from a running-WiFi clock tree), while HBN_RSV2
 * lives in the always-on domain and survives it. */
#endif

static BaseType_t cmd_reboot( char *out, size_t outlen, const char *cmd )
{
    BaseType_t plen = 0;
    const char * p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( ( p != NULL ) && ( plen == 8 ) && ( strncmp( p, "uartburn", 8 ) == 0 ) )
    {
#if defined( BL616 ) || defined( BL616CL )
        snprintf( out, outlen, "rebooting into UART ISP (bootrom download mode) ...\r\n" );
        vTaskDelay( pdMS_TO_TICKS( 50 ) );
        BL616_HBN_RSV2 = BL616_BOOT_SEL_ISP;
        bl_sys_reset_por();
        return pdFALSE;                  /* not reached */
#else
        /* bl602's boot ROM has no user_boot_sel — ISP entry is BOOT-pin-only on that chip. */
        snprintf( out, outlen, "uartburn: not supported on this chip (BOOT pin only)\r\n" );
        return pdFALSE;
#endif
    }

    snprintf( out, outlen, "rebooting ...\r\n" );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );   /* let the line flush before the reset fires */
    bl_sys_reset_por();
    return pdFALSE;                      /* not reached */
}

static const CLI_Command_Definition_t kBflbCmds[] = {
    { "reboot", "reboot [uartburn]:\r\n reset the chip (uartburn -> bootrom UART download mode)\r\n", cmd_reboot, -1 },
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
