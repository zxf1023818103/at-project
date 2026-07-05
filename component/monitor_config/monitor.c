/*
 * monitor.c — the cross-platform interactive console engine (FreeRTOS-Plus-CLI).
 * See monitor.h. Portable: all byte I/O is through component/console
 * (at_console_write_raw / at_console_getc via the platform RX seam), so this one
 * object serves AmebaD (KM0+KM4), Bouffalo BL602, and any newlib+at_console platform.
 * The per-platform RX backend, dual-core hand-off, reboot, and HW commands live in
 * component/<platform>/monitor_port and are threaded in via the monitor.h seam.
 */
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "console.h"     /* at_console_write_raw — un-timestamped console output */
#include "monitor.h"

/* Line buffer size. A pasted credential body (`set key`/`set cert`, ~1.6 KB base64)
 * arrives as one CLI argument, so a console that runs those needs headroom; KM0's SRAM
 * is tiny so it stays small. Arch-derived default; a platform may -D override. */
#ifndef MONITOR_CLI_MAX_INPUT
#  if defined( __ARM_ARCH_8M_MAIN__ )     /* AmebaD KM4 (Cortex-M33): big base64 pastes */
#    define MONITOR_CLI_MAX_INPUT   2560
#  elif defined( __ARM_ARCH_8M_BASE__ )   /* AmebaD KM0 (Cortex-M23): ~14 KB free SRAM */
#    define MONITOR_CLI_MAX_INPUT   80
#  else                                   /* others (e.g. BL602 RISC-V) */
#    define MONITOR_CLI_MAX_INPUT   512
#  endif
#endif
#define CLI_TASK_STACK  2048                    /* words (LFS file I/O in `set cert/key` needs headroom) */
#define CLI_TASK_PRIO   ( tskIDLE_PRIORITY + 2 )

/* ---- console output (raw: no log timestamp; via the active at_console backend) --- */

static void mon_puts( const char *s )
{
    at_console_write_raw( s, ( int ) strlen( s ) );
}

static void mon_putc( char c )
{
    at_console_write_raw( &c, 1 );
}

static void mon_prompt( void )
{
    mon_puts( "[" );
    mon_puts( monitor_core_name() );
    mon_puts( "] > " );
}

static void mon_banner( void )
{
    mon_puts( "\r\n[" );
    mon_puts( monitor_core_name() );
    mon_puts( "] FreeRTOS+CLI console - type 'help'.\r\n" );
}

/* ---- generic commands ---------------------------------------------------- */

static BaseType_t cmd_version( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    snprintf( out, outlen, "at-project %s - FreeRTOS %s\r\n",
              monitor_core_name(), tskKERNEL_VERSION_NUMBER );
    return pdFALSE;
}

static BaseType_t cmd_heap( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    /* %d (not %u): KM0's snprintf is the ROM DiagSnPrintf, which doesn't implement %u.
     * Heap sizes are small positive values, so signed prints the same. */
    snprintf( out, outlen, "free heap: %d bytes  (min ever: %d)\r\n",
              ( int ) xPortGetFreeHeapSize(),
              ( int ) xPortGetMinimumEverFreeHeapSize() );
    return pdFALSE;
}

static BaseType_t cmd_heapreset( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    /* Re-baseline the min-ever watermark to current free (isolate a later phase's
     * low-water). Only meaningful with CONFIG_MM_ENABLE_MIN_FREE_TRACKING; weak so a
     * heap without it links to a no-op instead of failing. */
    extern void vPortResetMinimumEverFreeHeapSize( void ) __attribute__( ( weak ) );
    if( vPortResetMinimumEverFreeHeapSize != NULL )
    {
        vPortResetMinimumEverFreeHeapSize();
    }
    snprintf( out, outlen, "min-ever heap watermark reset; now %d bytes free\r\n",
              ( int ) xPortGetMinimumEverFreeHeapSize() );
    return pdFALSE;
}

static BaseType_t cmd_tasks( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    const char *hdr = "Task           State  Prio  Stack  Num\r\n"
                      "----------------------------------------\r\n";
    size_t n = strlen( hdr );
    if( n >= outlen ) { n = outlen - 1; }
    memcpy( out, hdr, n );
    out[ n ] = '\0';
    vTaskList( out + n );               /* appends; buffer is configCOMMAND_INT_MAX_OUTPUT_SIZE */
    return pdFALSE;
}

static BaseType_t cmd_echo( char *out, size_t outlen, const char *cmd )
{
    BaseType_t plen = 0;
    const char *p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );
    if( p != NULL )
    {
        snprintf( out, outlen, "%.*s\r\n", ( int ) plen, p );
    }
    else
    {
        snprintf( out, outlen, "usage: echo <text>\r\n" );
    }
    return pdFALSE;
}

static const CLI_Command_Definition_t kCmds[] = {
    { "version",   "version:\r\n print firmware / kernel version + this core\r\n",  cmd_version,   0 },
    { "heap",      "heap:\r\n print free / minimum-ever heap\r\n",                  cmd_heap,      0 },
    { "heapreset", "heapreset:\r\n reset the min-ever heap watermark (needs CONFIG_MM_ENABLE_MIN_FREE_TRACKING)\r\n", cmd_heapreset, 0 },
    { "tasks",     "tasks:\r\n list the FreeRTOS tasks\r\n",                        cmd_tasks,     0 },
    { "echo",      "echo <text>:\r\n echo the argument back\r\n",                   cmd_echo,      1 },
};

/* ---- line processing + input loop ---------------------------------------- */

static void cli_run_line( char *line )
{
    char *out = FreeRTOS_CLIGetOutputBuffer();
    BaseType_t more;
    do
    {
        out[ 0 ] = '\0';
        more = FreeRTOS_CLIProcessCommand( line, out,
                                           configCOMMAND_INT_MAX_OUTPUT_SIZE );
        mon_puts( out );
    } while( more != pdFALSE );
}

static void cli_task( void *arg )
{
    static char line[ MONITOR_CLI_MAX_INPUT ];
    int idx = 0;
    ( void ) arg;

    for( ;; )
    {
        uint8_t c;
        int r = monitor_rx_get( &c );

        if( r < 0 )                     /* ownership (re)gained: fresh line + banner */
        {
            idx = 0;
            mon_banner();
            mon_prompt();
            continue;
        }
        if( r == 0 )                    /* idle / timeout */
        {
            continue;
        }

        if( c == '\r' || c == '\n' )
        {
            mon_puts( "\r\n" );
            line[ idx ] = '\0';
            if( idx > 0 )
            {
                cli_run_line( line );
            }
            idx = 0;
            if( monitor_after_command() )   /* a console hand-off happened -> wait for hand-back */
            {
                continue;
            }
            mon_prompt();
        }
        else if( c == '\b' || c == 0x7f )   /* backspace / DEL */
        {
            if( idx > 0 )
            {
                idx--;
                mon_puts( "\b \b" );
            }
        }
        else if( c >= 0x20 && c < 0x7f && idx < ( MONITOR_CLI_MAX_INPUT - 1 ) )
        {
            line[ idx++ ] = ( char ) c;
            mon_putc( ( char ) c );         /* echo */
        }
        /* other control bytes are ignored */
    }
}

void monitor_start( void )
{
    unsigned i;

    monitor_port_init();                    /* RX backend + console-ownership init */

    for( i = 0; i < sizeof( kCmds ) / sizeof( kCmds[ 0 ] ); i++ )
    {
        FreeRTOS_CLIRegisterCommand( &kCmds[ i ] );
    }

    /* The banner+prompt print from cli_task on first ownership (monitor_rx_get < 0). */
    if( xTaskCreate( cli_task, "cli", CLI_TASK_STACK, NULL,
                     CLI_TASK_PRIO, NULL ) != pdPASS )
    {
        mon_puts( "monitor: xTaskCreate(cli) failed\r\n" );
    }
}
