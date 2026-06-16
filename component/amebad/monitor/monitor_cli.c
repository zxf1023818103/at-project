/*
 * monitor_cli.c — the AmebaD console: FreeRTOS-Plus-CLI over the LOGUART, on BOTH cores,
 * with a KM0<->KM4 console hand-off. Replaces the legacy ROM/RAM AT-monitor shell.
 *
 * RX (KM4): IRQ-driven via a FreeRTOS StreamBuffer. The LOGUART RX interrupt (UART_LOG_IRQ) ISR
 * drains the HW RX FIFO into s_rx_stream and the console task blocks on xStreamBufferReceive. This
 * keeps the small HW FIFO empty even when a higher-priority task (WiFi) preempts the console task,
 * so a fast `set key`/`set cert` paste no longer overruns the FIFO and drops bytes (the old 10 ms
 * poll did). The LOGUART IMR is left as the ROM configured it (RX already enabled — that is why the
 * old code MASKED the line at the NVIC); registering a handler is what makes enabling the NVIC line
 * safe — the enabled-but-unhandled line is what hit the ROM "Irq 3 Fun Not Assign" storm
 * (memory amebad-loguart-irq-storm). RX (KM0): stays POLLED — its LOGUART RX IRQ (UART_LOG_IRQ_LP,
 * a separate NVIC) is the tickless wake source and its console is low-rate. Output + input echo go
 * out the same LOGUART via DiagPutChar (the device DiagPrintf/printf already use).
 *
 * Switch model: a logical "console owner" token. Only the OWNING core's task reads the LOGUART; on
 * KM4 it also OWNS the RX IRQ (enabled on acquire, masked on release), so the IRQ path composes
 * with the hand-off. Each core has its OWN g_owner (separate SRAM); the only cross-core signal is
 * IPC channel 0 (IPC_INT_CHAN_SHELL_SWITCH). KM4 boots owner, KM0 boots waiting. `>>` toggles to
 * the other core; `km0`/`km4` jump to a named core (no-op if already there).
 */
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "FreeRTOS_CLI.h"

#include "ameba_soc.h"   /* LOGUART_Readable/GetChar, DiagPutChar, WDG_*, ipc_send_message,
                          * IPC_INT_CHAN_SHELL_SWITCH, shell_switch_ipc_int decl (shell.h) */
#include "monitor.h"

/* `set key`/`set cert` pass a credential's ~1.6 KB base64 body as one CLI argument, so KM4's line
 * buffer must hold it. KM0 keeps 80: its SRAM is tiny (~14 KB free) and a 2.5 KB static line buffer
 * overflows it, corrupting shared state — symptom was KM4's runtime ITS flash write hitting a ROM
 * io_assert. KM4 = Cortex-M33 (ARMv8-M Mainline); KM0 = Cortex-M23 (Baseline). */
#if defined(__ARM_ARCH_8M_MAIN__)
#define CLI_MAX_INPUT   2560
#else
#define CLI_MAX_INPUT   80
#endif
#define CLI_TASK_STACK  2048                    /* words (LFS file I/O in `set cert/key` needs headroom) */
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

/* ---- low-level ROM commands (dw / ew / flash / efuse) -------------------- */
/* The AmebaD ROM (+ monitor_rom.c on KM4) provides cmd_dump_word / cmd_write_word / cmd_flash /
 * cmd_efuse with the classic (argc, argv) handler ABI; they print their own output via
 * MONITOR_LOG (DiagPrintf). Adapt a FreeRTOS+CLI line to that ABI: split the parameters into a
 * small argv[] of NUL-terminated tokens (argv[0] = first param, i.e. the command name is already
 * stripped by the CLI), call the handler, and leave the CLI output buffer empty. */
typedef u32 ( *mon_cmd_fn )( u16 argc, u8 *argv[] );

#define MON_MAX_ARGV  5
#define MON_ARG_LEN   68          /* fits an efuse 32-byte hex data string (64 chars) + NUL */

static BaseType_t run_rom_cmd( mon_cmd_fn fn, const char *cmd, char *out, size_t outlen )
{
    static char tok[ MON_MAX_ARGV ][ MON_ARG_LEN ];
    static u8  *argv[ MON_MAX_ARGV ];
    u16 argc = 0;

    ( void ) outlen;
    while( argc < MON_MAX_ARGV )
    {
        BaseType_t plen = 0;
        const char *p = FreeRTOS_CLIGetParameter( cmd, argc + 1, &plen );
        if( p == NULL ) { break; }
        if( plen > MON_ARG_LEN - 1 ) { plen = MON_ARG_LEN - 1; }
        memcpy( tok[ argc ], p, plen );
        tok[ argc ][ plen ] = '\0';
        argv[ argc ] = ( u8 * ) tok[ argc ];
        argc++;
    }
    out[ 0 ] = '\0';              /* the ROM handler prints directly via MONITOR_LOG */
    fn( argc, argv );
    return pdFALSE;
}

static BaseType_t cmd_dw( char *o, size_t n, const char *c )    { return run_rom_cmd( cmd_dump_word,  c, o, n ); }
static BaseType_t cmd_ew( char *o, size_t n, const char *c )    { return run_rom_cmd( cmd_write_word, c, o, n ); }
static BaseType_t cmd_flash_( char *o, size_t n, const char *c ){ return run_rom_cmd( cmd_flash,      c, o, n ); }
static BaseType_t cmd_efuse_( char *o, size_t n, const char *c ){ return run_rom_cmd( cmd_efuse,      c, o, n ); }

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
    { "dw",      "dw <hexaddr>:\r\n dump a memory dword\r\n",                     cmd_dw,        -1 },
    { "ew",      "ew <hexaddr> <hexval>:\r\n write a memory dword\r\n",           cmd_ew,        -1 },
    { "flash",   "flash <erase|read|write|...> ...:\r\n flash ops (see SDK cmd_flash)\r\n", cmd_flash_, -1 },
    { "efuse",   "efuse <wmap|rmap|wraw|rraw> ...:\r\n efuse ops (wmap/wraw BURN efuse)\r\n", cmd_efuse_, -1 },
    { ">>",      ">>:\r\n hand the console to the other core\r\n",                cmd_sw_toggle, 0 },
    { "km0",     "km0:\r\n switch the console to KM0\r\n",                        cmd_sw_km0,    0 },
    { "km4",     "km4:\r\n switch the console to KM4\r\n",                        cmd_sw_km4,    0 },
};

/* ---- LOGUART RX: IRQ + StreamBuffer (KM4) / polling (KM0) ----------------- *
 * cli_rx_enable()/disable() follow console ownership; cli_rx_get() fetches the next byte. */
#if defined(__ARM_ARCH_8M_MAIN__)

#define CLI_RX_STREAM_SZ   1024     /* SW FIFO (heap); absorbs a paced `set key` line under WiFi
                                     * preemption. 1 KB: the host paces 16 B/6 ms and the cli task
                                     * drains fast, and the NS heap is tight for the TLS handshake. */
static StreamBufferHandle_t s_rx_stream;

/* LOGUART RX ISR: drain the whole HW FIFO into the StreamBuffer (reading it deasserts the
 * level-triggered RX-data/timeout interrupt). Priority 5 == configLIBRARY_MAX_SYSCALL_INTERRUPT_
 * PRIORITY on KM4 — the most urgent level still allowed to call ...FromISR.
 *
 * RAM-resident (AN0400 §18.2): this ISR can fire while the SPE is programming flash (SPIC in user
 * mode), when XIP instruction fetch is unavailable — running it (and the StreamBuffer functions it
 * calls) from XIP would fault. IMAGE2_RAM_TEXT_SECTION pins this function in SRAM; the StreamBuffer
 * + FromISR-critical functions are pulled into .ram_image2.text by the ld (rlx8721d_img2_is.ld),
 * and memcpy is aliased to the ROM _memcpy (app_km4/CMakeLists.txt --defsym). */
IMAGE2_RAM_TEXT_SECTION
static u32 cli_loguart_rx_isr( void *data )
{
    BaseType_t woken = pdFALSE;
    ( void ) data;
    while( LOGUART_Readable() )
    {
        u8 c = LOGUART_GetChar( _FALSE );
        ( void ) xStreamBufferSendFromISR( s_rx_stream, &c, 1, &woken );
    }
    portYIELD_FROM_ISR( woken );
    return 0;
}

/* Claim the RX IRQ (this core just became console owner): drop bytes the FIFO/stream collected
 * for the previous owner (clean line start), then unmask the NVIC line. */
static void cli_rx_enable( void )
{
    while( LOGUART_Readable() ) { ( void ) LOGUART_GetChar( _FALSE ); }
    if( s_rx_stream != NULL ) { ( void ) xStreamBufferReset( s_rx_stream ); }
    InterruptEn( UART_LOG_IRQ, 5 );
}

static void cli_rx_disable( void )      /* release the RX IRQ (mask at the NVIC) */
{
    InterruptDis( UART_LOG_IRQ );
}

/* Block for the next RX byte; 1 if read, 0 on timeout (lets the loop re-check g_owner). */
static int cli_rx_get( u8 *c )
{
    return ( xStreamBufferReceive( s_rx_stream, c, 1, pdMS_TO_TICKS( 100 ) ) == 1 ) ? 1 : 0;
}

#else  /* KM0: polling (the LP LOGUART RX IRQ is the tickless wake source — leave it alone) */

static void cli_rx_enable( void )  { }
static void cli_rx_disable( void ) { InterruptDis( UART_LOG_IRQ ); }
static int cli_rx_get( u8 *c )
{
    if( !LOGUART_Readable() ) { vTaskDelay( pdMS_TO_TICKS( 10 ) ); return 0; }
    *c = LOGUART_GetChar( _TRUE );
    return 1;
}

#endif

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

    if( g_owner )
    {
        cli_rx_enable();        /* initial owner (KM4): take the RX IRQ now the scheduler runs */
    }

    for( ;; )
    {
        u8 c;

        if( !g_owner )
        {
            /* Not the console owner: release the RX IRQ to the other core and wait for the IPC
             * hand-off (g_owner set by shell_switch_ipc_int on this core). */
            cli_rx_disable();
            idx = 0;
            while( !g_owner )
            {
                vTaskDelay( pdMS_TO_TICKS( 50 ) );
            }
            cli_rx_enable();    /* claim the console RX now we own it */
            cli_puts( "\r\n[" );
            cli_puts( amebad_monitor_core_name() );
            cli_puts( "] console - type 'help'.\r\n" );
            cli_prompt();
            continue;
        }

        if( !cli_rx_get( &c ) )         /* no byte before the timeout: re-check g_owner */
        {
            continue;
        }

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
                cli_rx_disable();       /* stop taking RX before handing the console off */
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

#if defined(__ARM_ARCH_8M_MAIN__)
    /* KM4: IRQ-driven console RX. Create the SW FIFO + install the LOGUART RX ISR; the line stays
     * masked here and cli_task's cli_rx_enable() unmasks it once it owns the console (and the
     * scheduler is running). Registering a handler is what makes enabling the line safe — without
     * one the enabled RX line hits the ROM "Irq 3 Fun Not Assign" storm (see the file header). */
    s_rx_stream = xStreamBufferCreate( CLI_RX_STREAM_SZ, 1 );   /* trigger level 1 byte */
    if( s_rx_stream == NULL )
    {
        DiagPrintf( "[%s] cli RX StreamBuffer alloc failed\n\r", amebad_monitor_core_name() );
    }
    else
    {
        InterruptRegister( cli_loguart_rx_isr, UART_LOG_IRQ, 0, 5 );
    }
#endif

    /* Mask the LOGUART RX IRQ (IRQ 3) on THIS core to start: cli_rx_enable() unmasks it for the
     * owner (KM4 IRQ path); on KM0 it stays masked the whole run (polling). Masked, the RX
     * hardware still fills the FIFO, so KM0's LOGUART_Readable() poll still sees bytes. Also keeps
     * an enabled-but-unhandled line from triggering the ROM "Irq 3 Fun Not Assign" storm before
     * the owner is set up. UART_LOG_IRQ == UART_LOG_IRQ_LP == 3; InterruptDis is per-core NVIC. */
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
