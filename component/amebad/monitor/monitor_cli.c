/*
 * monitor_cli.c — the AmebaD console: FreeRTOS-Plus-CLI over the LOGUART, on BOTH cores,
 * with a KM0<->KM4 console hand-off. Replaces the legacy ROM/RAM AT-monitor shell.
 *
 * Input is POLLED (LOGUART_Readable + LOGUART_GetChar) from the console task — the LOGUART RX
 * IRQ is never enabled, which is what avoids the ROM "Irq 3 Fun Not Assign" storm an
 * enabled-but-unhandled RX line causes (memory amebad-loguart-irq-storm). Output + the input
 * echo go out the same LOGUART via DiagPutChar (the device DiagPrintf/printf already use).
 *
 * Switch model (polling-safe): there is no RX IRQ to hand off, so we hand off a logical
 * "console owner" token. Only the OWNING core's task reads the LOGUART; the other core's task
 * just waits. Each core has its OWN g_owner (separate SRAM); the only cross-core signal is IPC
 * channel 0 (IPC_INT_CHAN_SHELL_SWITCH). KM4 boots owner, KM0 boots waiting. `>>` toggles to
 * the other core; `km0`/`km4` jump to a named core (no-op if already there).
 */
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "ameba_soc.h"   /* LOGUART_Readable/GetChar, DiagPutChar, WDG_*, ipc_send_message,
                          * IPC_INT_CHAN_SHELL_SWITCH, shell_switch_ipc_int decl (shell.h) */
#include "monitor.h"

#define CLI_MAX_INPUT   80
#define CLI_TASK_STACK  1024                    /* words */
#define CLI_TASK_PRIO   ( tskIDLE_PRIORITY + 2 )

/* Console-owner token (per core; KM4 = 1 at boot, KM0 = 0). Set to 1 by the IPC handler on the
 * core that is being handed the console. The non-owner task polls this and stays off the UART. */
static volatile int g_owner;
/* A switch command sets this; the task loop performs the hand-off after the CLI callback (not
 * inside it, so the "switching..." line is flushed first). */
static volatile int g_want_switch;

/* ---- byte I/O on the LOGUART (same device DiagPrintf uses) ---------------- */

static void cli_puts( const char *s )
{
    while( *s )
    {
        DiagPutChar( *s++ );
    }
}

static void cli_prompt( void )
{
    cli_puts( "[" );
    cli_puts( amebad_monitor_core_name() );
    cli_puts( "] > " );
}

/* ---- baseline commands --------------------------------------------------- */

static BaseType_t cmd_version( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    snprintf( out, outlen, "at-project AmebaD %s - FreeRTOS %s\r\n",
              amebad_monitor_core_name(), tskKERNEL_VERSION_NUMBER );
    return pdFALSE;
}

static BaseType_t cmd_heap( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    /* %d (not %u): KM0's snprintf is the ROM DiagSnPrintf, which doesn't implement %u. Heap
     * sizes are small positive values, so signed prints the same. */
    snprintf( out, outlen, "free heap: %d bytes  (min ever: %d)\r\n",
              ( int ) xPortGetFreeHeapSize(),
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
    vTaskList( out + n );               /* appends; output buffer is configCOMMAND_INT_MAX_OUTPUT_SIZE */
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

static BaseType_t cmd_reboot( char *out, size_t outlen, const char *cmd )
{
    WDG_InitTypeDef WDG_InitStruct;
    u32 CountProcess, DivFacProcess;
    BaseType_t plen = 0;
    const char *p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( p != NULL && plen == 8 && strncmp( p, "uartburn", 8 ) == 0 )
    {
        /* Set the backup-register bit the boot ROM checks on the next reset to enter UART
         * download mode (BIT_UARTBURN_BOOT survives the reset — it's in the always-on domain). */
        BKUP_Set( 0, BIT_UARTBURN_BOOT );
        snprintf( out, outlen, "rebooting %s into UART download mode ...\r\n",
                  amebad_monitor_core_name() );
    }
    else
    {
        snprintf( out, outlen, "rebooting %s ...\r\n", amebad_monitor_core_name() );
    }
    /* 5 ms watchdog reset — identical sequence on KM4 and KM0. The 5 ms window is ample for the
     * message above to flush at 1.5 Mbaud before the reset fires. */
    WDG_Scalar( 5, &CountProcess, &DivFacProcess );
    WDG_InitStruct.CountProcess  = CountProcess;
    WDG_InitStruct.DivFacProcess = DivFacProcess;
    WDG_Init( &WDG_InitStruct );
    WDG_Cmd( ENABLE );
    return pdFALSE;
}

/* ---- console hand-off commands ( >> / km0 / km4 ) ------------------------- */

/* target == NULL -> toggle (always hand off); else hand off iff `target` names the OTHER core. */
static BaseType_t do_switch( char *out, size_t outlen, const char *target )
{
    const char *me    = amebad_monitor_core_name();
    const char *other = ( strcmp( me, "KM4" ) == 0 ) ? "KM0" : "KM4";

    if( target != NULL && strcmp( target, me ) == 0 )
    {
        snprintf( out, outlen, "already on %s\r\n", me );
        return pdFALSE;
    }
    g_want_switch = 1;                          /* the task loop performs the hand-off */
    snprintf( out, outlen, "-> handing console to %s ...\r\n", other );
    return pdFALSE;
}

static BaseType_t cmd_sw_toggle( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    return do_switch( out, outlen, NULL );
}

static BaseType_t cmd_sw_km0( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    return do_switch( out, outlen, "KM0" );
}

static BaseType_t cmd_sw_km4( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    return do_switch( out, outlen, "KM4" );
}

static const CLI_Command_Definition_t kCmds[] = {
    { "version", "version:\r\n print firmware / kernel version + this core\r\n",  cmd_version,   0 },
    { "heap",    "heap:\r\n print free / minimum-ever heap\r\n",                  cmd_heap,      0 },
    { "tasks",   "tasks:\r\n list the FreeRTOS tasks\r\n",                        cmd_tasks,     0 },
    { "echo",    "echo <text>:\r\n echo the argument back\r\n",                   cmd_echo,      1 },
    { "reboot",  "reboot [uartburn]:\r\n watchdog-reset this core (uartburn -> UART download mode)\r\n", cmd_reboot, -1 },
    { ">>",      ">>:\r\n hand the console to the other core\r\n",                cmd_sw_toggle, 0 },
    { "km0",     "km0:\r\n switch the console to KM0\r\n",                        cmd_sw_km0,    0 },
    { "km4",     "km4:\r\n switch the console to KM4\r\n",                        cmd_sw_km4,    0 },
};

/* ---- line processing + input loop --------------------------------------- */

static void cli_run_line( char *line )
{
    char *out = FreeRTOS_CLIGetOutputBuffer();
    BaseType_t more;
    do
    {
        out[ 0 ] = '\0';
        more = FreeRTOS_CLIProcessCommand( line, out,
                                           configCOMMAND_INT_MAX_OUTPUT_SIZE );
        cli_puts( out );
    } while( more != pdFALSE );
}

static void cli_task( void *arg )
{
    static char line[ CLI_MAX_INPUT ];
    int idx = 0;
    ( void ) arg;

    for( ;; )
    {
        u8 c;

        if( !g_owner )
        {
            /* Not the console owner: stay off the shared LOGUART and wait for the IPC
             * hand-off (g_owner set by shell_switch_ipc_int on this core). */
            idx = 0;
            while( !g_owner )
            {
                vTaskDelay( pdMS_TO_TICKS( 50 ) );
            }
            cli_puts( "\r\n[" );
            cli_puts( amebad_monitor_core_name() );
            cli_puts( "] console - type 'help'.\r\n" );
            cli_prompt();
            continue;
        }

        if( !LOGUART_Readable() )       /* poll: never enables the RX IRQ */
        {
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
            continue;
        }
        c = LOGUART_GetChar( _TRUE );

        if( c == '\r' || c == '\n' )
        {
            DiagPutChar( '\r' );
            DiagPutChar( '\n' );
            line[ idx ] = '\0';
            if( idx > 0 )
            {
                cli_run_line( line );
            }
            idx = 0;
            if( g_want_switch )
            {
                /* Give up the console, then signal the other core to take over. */
                g_want_switch = 0;
                g_owner = 0;
                ipc_send_message( IPC_INT_CHAN_SHELL_SWITCH, ( u32 ) NULL );
                continue;               /* -> top of loop -> wait for hand-back */
            }
            cli_prompt();
        }
        else if( c == '\b' || c == 0x7f )       /* backspace / DEL */
        {
            if( idx > 0 )
            {
                idx--;
                DiagPutChar( '\b' );
                DiagPutChar( ' ' );
                DiagPutChar( '\b' );
            }
        }
        else if( c >= 0x20 && c < 0x7f && idx < ( CLI_MAX_INPUT - 1 ) )
        {
            line[ idx++ ] = ( char ) c;
            DiagPutChar( c );                   /* echo */
        }
        /* other control bytes are ignored */
    }
}

/* IPC channel-0 handler (registered by usrcfg/rtl8721d_ipccfg.c on both cores). Runs on the
 * core being HANDED the console when the other core gives it up: just claim ownership. A plain
 * volatile write — no FreeRTOS-from-ISR call — so it is safe regardless of the IPC IRQ priority;
 * the waiting cli_task polls g_owner and takes over within one 50 ms tick. */
void shell_switch_ipc_int( VOID *Data, u32 IrqStatus, u32 ChanNum )
{
    ( void ) Data;
    ( void ) IrqStatus;
    ( void ) ChanNum;
    g_owner = 1;
}

/* Register the commands and spawn the console task. Called from each core's main.c. */
void amebad_monitor_start( void )
{
    unsigned i;

    /* We POLL the LOGUART RX (LOGUART_Readable). Mask the LOGUART RX IRQ (IRQ 3) on THIS core so
     * the ROM boot-console ISR doesn't drain the RX FIFO out from under the poll (and so an
     * unhandled-but-enabled line can't trigger the ROM "Irq 3 Fun Not Assign" storm). The RX
     * hardware keeps filling the FIFO with the IRQ masked, so LOGUART_Readable() still sees bytes.
     * UART_LOG_IRQ == UART_LOG_IRQ_LP == 3; InterruptDis acts on the current core's NVIC. */
    InterruptDis( UART_LOG_IRQ );

    /* KM4 owns the console at boot; KM0 waits for the user to hand it over. */
    g_owner = ( strcmp( amebad_monitor_core_name(), "KM4" ) == 0 ) ? 1 : 0;

    for( i = 0; i < sizeof( kCmds ) / sizeof( kCmds[ 0 ] ); i++ )
    {
        FreeRTOS_CLIRegisterCommand( &kCmds[ i ] );
    }

    if( g_owner )
    {
        cli_puts( "\r\n[" );
        cli_puts( amebad_monitor_core_name() );
        cli_puts( "] FreeRTOS+CLI console - type 'help'.\r\n" );
        cli_prompt();
    }

    if( xTaskCreate( cli_task, "cli", CLI_TASK_STACK, NULL,
                     CLI_TASK_PRIO, NULL ) != pdPASS )
    {
        DiagPrintf( "[%s] xTaskCreate(cli) failed\n\r", amebad_monitor_core_name() );
    }
}
