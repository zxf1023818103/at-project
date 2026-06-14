/*
 * cli_task.c — FreeRTOS-Plus-CLI interactive console on the AmebaD LOGUART.
 *
 * Enabled by AMEBAD_ENABLE_CLI (make CLI=ON); main.c starts it under AT_PROJECT_CLI
 * INSTEAD OF the ROM/RAM AT-monitor shell. The two cannot coexist: the monitor owns
 * the LOGUART RX via an IRQ-3 handler (shell_uart_irq_rom). This CLI takes the same
 * UART but drives it by POLLING (LOGUART_Readable + LOGUART_GetChar) — it never enables
 * the LOGUART RX IRQ, which is what avoids the ROM "Irq 3 Fun Not Assign" storm an
 * unhandled-but-enabled RX line causes (see doc / memory amebad-loguart-irq-storm).
 *
 * Output and the input echo go out the same LOGUART via DiagPutChar (the console that
 * DiagPrintf/printf already use). The task reads one logical line (with backspace
 * editing), then runs the FreeRTOS+CLI process loop and writes each output chunk.
 */
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "ameba_soc.h"   /* LOGUART_Readable / LOGUART_GetChar / DiagPutChar / _TRUE */

#define CLI_MAX_INPUT   80
#define CLI_TASK_STACK  1024            /* words */
#define CLI_TASK_PRIO   ( tskIDLE_PRIORITY + 2 )

/* ---- byte I/O on the LOGUART (same device DiagPrintf uses) ---------------- */

static void cli_puts( const char *s )
{
    while( *s )
    {
        DiagPutChar( *s++ );
    }
}

/* ---- sample commands ----------------------------------------------------- */

static BaseType_t cmd_version( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    snprintf( out, outlen, "at-project AmebaD KM4 - FreeRTOS %s\r\n",
              tskKERNEL_VERSION_NUMBER );
    return pdFALSE;
}

static BaseType_t cmd_heap( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    snprintf( out, outlen, "free heap: %u bytes  (min ever: %u)\r\n",
              ( unsigned ) xPortGetFreeHeapSize(),
              ( unsigned ) xPortGetMinimumEverFreeHeapSize() );
    return pdFALSE;
}

static BaseType_t cmd_tasks( char *out, size_t outlen, const char *cmd )
{
    ( void ) cmd;
    /* configUSE_TRACE_FACILITY + configUSE_STATS_FORMATTING_FUNCTIONS are on. */
    const char *hdr = "Task           State  Prio  Stack  Num\r\n"
                      "----------------------------------------\r\n";
    size_t n = strlen( hdr );
    if( n >= outlen ) { n = outlen - 1; }
    memcpy( out, hdr, n );
    out[ n ] = '\0';
    vTaskList( out + n );               /* appends; output buffer is 2 KB */
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
    { "version", "version:\r\n print firmware / kernel version\r\n",       cmd_version, 0 },
    { "heap",    "heap:\r\n print free / minimum-ever heap\r\n",           cmd_heap,    0 },
    { "tasks",   "tasks:\r\n list the FreeRTOS tasks\r\n",                 cmd_tasks,   0 },
    { "echo",    "echo <text>:\r\n echo the argument back\r\n",           cmd_echo,    1 },
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

    cli_puts( "\r\nFreeRTOS+CLI on LOGUART - type 'help'.\r\n> " );

    for( ;; )
    {
        u8 c;

        if( !LOGUART_Readable() )       /* poll: never enables the RX IRQ */
        {
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
            continue;
        }
        c = LOGUART_GetChar( _TRUE );   /* a byte is waiting -> returns at once */

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
            cli_puts( "> " );
        }
        else if( c == '\b' || c == 0x7f )      /* backspace / DEL */
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

/* Register the sample commands and spawn the input task. Called from main.c
 * (under AT_PROJECT_CLI) in place of shell_init_rom/shell_init_ram. */
void amebad_cli_start( void )
{
    unsigned i;
    for( i = 0; i < sizeof( kCmds ) / sizeof( kCmds[ 0 ] ); i++ )
    {
        FreeRTOS_CLIRegisterCommand( &kCmds[ i ] );
    }
    if( xTaskCreate( cli_task, "cli", CLI_TASK_STACK, NULL,
                     CLI_TASK_PRIO, NULL ) != pdPASS )
    {
        DiagPrintf( "[KM4] xTaskCreate(cli) failed\n\r" );
    }
}
