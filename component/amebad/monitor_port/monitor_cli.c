/*
 * monitor_cli.c — the AmebaD platform port for the cross-platform console engine
 * (component/monitor_config). Implements the monitor.h seam (monitor_port_init /
 * monitor_rx_get / monitor_after_command) plus the AmebaD-only commands, over the
 * LOGUART:
 *   - RX (KM4): IRQ-driven via a FreeRTOS StreamBuffer (cli_loguart_rx_isr) so a fast
 *     `set key`/`set cert` base64 paste never overruns the small HW FIFO. RX (KM0):
 *     polled (its LOGUART RX IRQ is the tickless wake source). See memory
 *     amebad-loguart-irq-storm for why an enabled-but-unhandled RX line must be avoided.
 *   - Console hand-off: a per-core "owner" token; only the owning core reads the LOGUART.
 *     `>>`/`km0`/`km4` switch cores over IPC channel 0 (IPC_INT_CHAN_SHELL_SWITCH). This
 *     ownership lives entirely behind monitor_rx_get()/monitor_after_command(), so the
 *     shared engine stays core-agnostic.
 *   - Commands: reboot (WDG + UART-download backup bit), and dw/ew/flash/efuse via the
 *     ROM (argc,argv) handlers (monitor_rom.c on KM4 / mask ROM on KM0). Registered by
 *     amebad_monitor_cmds_register() from main.c after monitor_start().
 */
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "FreeRTOS_CLI.h"

#include "ameba_soc.h"   /* LOGUART_Readable/GetChar, DiagPutChar, WDG_*, ipc_send_message,
                          * IPC_INT_CHAN_SHELL_SWITCH, shell_switch_ipc_int decl (shell.h) */
#include "monitor.h"     /* the shared engine seam (component/monitor_config) */

/* Console-owner token (per core; KM4 = 1 at boot, KM0 = 0). Set to 1 by the IPC handler
 * on the core being handed the console. s_active tracks whether our RX is enabled. */
static volatile int g_owner;
static volatile int g_want_switch;
static int          s_active;

/* ---- LOGUART RX: IRQ + StreamBuffer (KM4) / polling (KM0) ----------------- */
#if defined( __ARM_ARCH_8M_MAIN__ )

#define CLI_RX_STREAM_SZ   1024     /* SW FIFO (heap); absorbs a paced `set key` line under WiFi
                                     * preemption (host paces 16 B/6 ms; the cli task drains fast). */
static StreamBufferHandle_t s_rx_stream;

/* LOGUART RX ISR: drain the whole HW FIFO into the StreamBuffer (reading it deasserts the
 * level-triggered RX interrupt). Priority 5 = the most urgent still allowed to call ...FromISR.
 * RAM-resident (IMAGE2_RAM_TEXT_SECTION): can fire while the SPE programs flash (XIP off). */
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

static void cli_rx_enable( void )   /* claim RX: drop the previous owner's bytes, unmask the NVIC line */
{
    while( LOGUART_Readable() ) { ( void ) LOGUART_GetChar( _FALSE ); }
    if( s_rx_stream != NULL ) { ( void ) xStreamBufferReset( s_rx_stream ); }
    InterruptEn( UART_LOG_IRQ, 5 );
}

static void cli_rx_disable( void )  { InterruptDis( UART_LOG_IRQ ); }

static int cli_rx_get( u8 *c )      /* block for a byte; 1 = read, 0 = timeout (re-check owner) */
{
    return ( xStreamBufferReceive( s_rx_stream, c, 1, pdMS_TO_TICKS( 100 ) ) == 1 ) ? 1 : 0;
}

#else  /* KM0: polling (the LP LOGUART RX IRQ is the tickless wake source — leave it alone) */

static void cli_rx_enable( void )   { }
static void cli_rx_disable( void )  { InterruptDis( UART_LOG_IRQ ); }
static int cli_rx_get( u8 *c )
{
    if( !LOGUART_Readable() ) { vTaskDelay( pdMS_TO_TICKS( 10 ) ); return 0; }
    *c = LOGUART_GetChar( _TRUE );
    return 1;
}

#endif

/* ===== monitor.h seam ====================================================== */

void monitor_port_init( void )
{
#if defined( __ARM_ARCH_8M_MAIN__ )
    /* KM4: create the SW FIFO + install the LOGUART RX ISR. The line stays masked until
     * cli_rx_enable() unmasks it for the owner. Registering a handler is what makes
     * enabling the line safe (else the enabled RX line hits the ROM "Irq 3 Fun Not
     * Assign" storm). */
    s_rx_stream = xStreamBufferCreate( CLI_RX_STREAM_SZ, 1 );
    if( s_rx_stream == NULL )
    {
        DiagPrintf( "[%s] cli RX StreamBuffer alloc failed\n\r", monitor_core_name() );
    }
    else
    {
        InterruptRegister( cli_loguart_rx_isr, UART_LOG_IRQ, 0, 5 );
    }
#endif
    /* Mask the LOGUART RX IRQ on THIS core to start; the owner unmasks via cli_rx_enable()
     * (KM4), KM0 stays masked (polling). UART_LOG_IRQ == UART_LOG_IRQ_LP == 3. */
    InterruptDis( UART_LOG_IRQ );

    /* KM4 owns the console at boot; KM0 waits for the user to hand it over. */
    g_owner  = ( strcmp( monitor_core_name(), "KM4" ) == 0 ) ? 1 : 0;
    s_active = 0;
}

int monitor_rx_get( uint8_t *c )
{
    if( !g_owner )
    {
        /* Not the console owner: release RX to the other core, wait for the IPC hand-off. */
        if( s_active ) { cli_rx_disable(); s_active = 0; }
        while( !g_owner ) { vTaskDelay( pdMS_TO_TICKS( 50 ) ); }
        cli_rx_enable(); s_active = 1;
        return -1;                  /* ownership gained -> engine reprints the banner */
    }
    if( !s_active )                 /* first time as owner (boot, or hand-back) -> claim RX */
    {
        cli_rx_enable(); s_active = 1;
        return -1;
    }
    return cli_rx_get( c ) ? 1 : 0;
}

int monitor_after_command( void )
{
    if( !g_want_switch )
    {
        return 0;
    }
    /* Give up the console, then signal the other core to take over (after the CLI callback
     * so the "switching..." line is flushed first). */
    g_want_switch = 0;
    cli_rx_disable(); s_active = 0;
    g_owner = 0;
    ipc_send_message( IPC_INT_CHAN_SHELL_SWITCH, ( u32 ) NULL );
    return 1;                       /* engine skips the prompt and loops -> waits for hand-back */
}

/* IPC channel-0 handler (registered by usrcfg/rtl8721d_ipccfg.c on both cores). Runs on the
 * core being HANDED the console: just claim ownership. Plain volatile write — safe from any
 * IRQ priority; the waiting cli_task's monitor_rx_get polls g_owner within one 50 ms tick. */
void shell_switch_ipc_int( VOID *Data, u32 IrqStatus, u32 ChanNum )
{
    ( void ) Data;
    ( void ) IrqStatus;
    ( void ) ChanNum;
    g_owner = 1;
}

/* ===== AmebaD-only commands ================================================ */

static BaseType_t cmd_reboot( char *out, size_t outlen, const char *cmd )
{
    WDG_InitTypeDef WDG_InitStruct;
    u32 CountProcess, DivFacProcess;
    BaseType_t plen = 0;
    const char *p = FreeRTOS_CLIGetParameter( cmd, 1, &plen );

    if( p != NULL && plen == 8 && strncmp( p, "uartburn", 8 ) == 0 )
    {
        /* Backup-register bit the boot ROM checks on the next reset to enter UART download
         * mode (survives the reset — it's in the always-on domain). */
        BKUP_Set( 0, BIT_UARTBURN_BOOT );
        snprintf( out, outlen, "rebooting %s into UART download mode ...\r\n", monitor_core_name() );
    }
    else
    {
        snprintf( out, outlen, "rebooting %s ...\r\n", monitor_core_name() );
    }
    /* 5 ms watchdog reset — ample for the message above to flush before the reset fires. */
    WDG_Scalar( 5, &CountProcess, &DivFacProcess );
    WDG_InitStruct.CountProcess  = CountProcess;
    WDG_InitStruct.DivFacProcess = DivFacProcess;
    WDG_Init( &WDG_InitStruct );
    WDG_Cmd( ENABLE );
    return pdFALSE;
}

/* ---- low-level ROM commands (dw / ew / flash / efuse) -------------------- *
 * The ROM (+ monitor_rom.c on KM4) provides cmd_dump_word/cmd_write_word/cmd_flash/cmd_efuse
 * with the classic (argc, argv) ABI; they print their own output via MONITOR_LOG (DiagPrintf).
 * Adapt a FreeRTOS+CLI line to that ABI and leave the CLI output buffer empty. */
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

/* target == NULL -> toggle; else hand off iff `target` names the OTHER core. */
static BaseType_t do_switch( char *out, size_t outlen, const char *target )
{
    const char *me    = monitor_core_name();
    const char *other = ( strcmp( me, "KM4" ) == 0 ) ? "KM0" : "KM4";

    if( target != NULL && strcmp( target, me ) == 0 )
    {
        snprintf( out, outlen, "already on %s\r\n", me );
        return pdFALSE;
    }
    g_want_switch = 1;                          /* the engine performs the hand-off after this callback */
    snprintf( out, outlen, "-> handing console to %s ...\r\n", other );
    return pdFALSE;
}

static BaseType_t cmd_sw_toggle( char *out, size_t outlen, const char *cmd ) { ( void ) cmd; return do_switch( out, outlen, NULL ); }
static BaseType_t cmd_sw_km0( char *out, size_t outlen, const char *cmd )    { ( void ) cmd; return do_switch( out, outlen, "KM0" ); }
static BaseType_t cmd_sw_km4( char *out, size_t outlen, const char *cmd )    { ( void ) cmd; return do_switch( out, outlen, "KM4" ); }

static const CLI_Command_Definition_t kAmebadCmds[] = {
    { "reboot",  "reboot [uartburn]:\r\n watchdog-reset this core (uartburn -> UART download mode)\r\n", cmd_reboot, -1 },
    { "dw",      "dw <hexaddr>:\r\n dump a memory dword\r\n",                     cmd_dw,        -1 },
    { "ew",      "ew <hexaddr> <hexval>:\r\n write a memory dword\r\n",           cmd_ew,        -1 },
    { "flash",   "flash <erase|read|write|status|cache|lock|unlock> ...:\r\n flash ops; lock <fw|all> / unlock = SR write-protect (KM4)\r\n", cmd_flash_, -1 },
    { "efuse",   "efuse <wmap|rmap|wraw|rraw> ...:\r\n efuse ops (wmap/wraw BURN efuse)\r\n", cmd_efuse_, -1 },
    { ">>",      ">>:\r\n hand the console to the other core\r\n",                cmd_sw_toggle, 0 },
    { "km0",     "km0:\r\n switch the console to KM0\r\n",                        cmd_sw_km0,    0 },
    { "km4",     "km4:\r\n switch the console to KM4\r\n",                        cmd_sw_km4,    0 },
};

/* Called from main.c after monitor_start() (the register-after-start pattern). */
void amebad_monitor_cmds_register( void )
{
    unsigned i;
    for( i = 0; i < sizeof( kAmebadCmds ) / sizeof( kAmebadCmds[ 0 ] ); i++ )
    {
        FreeRTOS_CLIRegisterCommand( &kAmebadCmds[ i ] );
    }
}
