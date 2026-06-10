/*
 * console_lpuart.c — route stdin/stdout to the LPUART (UART3, PB1 TX / PB2 RX),
 * leaving stderr on the LOGUART.
 *
 * The console defaults to the LOGUART (console_loguart.c). Calling
 * amebad_console_init_lpuart() once at startup brings up UART3 via the fwlib UART
 * HAL (all ROM _LONG_CALL_ entries) and registers an LPUART backend with the
 * shared console dispatcher (component/console, at_console_set_backend), so after:
 *
 *   stdin/stdout (fd 0/1)  -> LPUART  (host COM7)        -> std::cin / std::cout
 *   stderr       (fd 2)    -> LOGUART (the log console)  -> std::cerr / DiagPrintf
 *
 * Gated into the build (AMEBAD_CPP_SELFTEST). Uses the fwlib HAL + UART3_DEV
 * directly (UART_Init/CharPut/CharGet are ROM-provided — see fwlib_rom_km4.ld),
 * so no extra driver TU is linked. KM4-only.
 */
#include "ameba_soc.h"       /* fwlib UART HAL + Pinmux_Config + UART3_DEV + _PB_1/_PB_2 */
#include "console.h"         /* at_console_set_backend + at_console_backend_t */

/* PB1=TX / PB2=RX -> UART3 (LPUART). 115200 8N1 to match the COM7 host setup. */
#define LPUART_DEV      UART3_DEV
#define LPUART_TX_PIN   _PB_1
#define LPUART_RX_PIN   _PB_2
#define LPUART_BAUD     115200

/* DiagPrintf is declared by ameba_soc.h */

#define LP_SPIN_MAX 2000000u   /* bounded so a dead UART can't hang the image */

static void lp_putc(char c)
{
    u32 n = LP_SPIN_MAX;
    while (!UART_Writable(LPUART_DEV) && --n) { }
    if (n) { UART_CharPut(LPUART_DEV, (u8)c); }
}

static int lp_getc(void)
{
    u32 n = LP_SPIN_MAX;
    u8 b;
    while (!UART_Readable(LPUART_DEV) && --n) { }
    if (!n) { return -1; }
    UART_CharGet(LPUART_DEV, &b);
    return (int)b;
}

static void lp_errputc(char c) { DiagPutChar(c); }   /* stderr stays on LOGUART */

/* LPUART for stdin/stdout, LOGUART for stderr — handed to the shared dispatcher. */
static const at_console_backend_t LPUART_BACKEND = {
    lp_putc,      /* putc    -> LPUART  */
    lp_getc,      /* getc    <- LPUART  */
    lp_errputc,   /* errputc -> LOGUART */
};

/* Replicate the mbed serial pin_function(): set the pad's UART function AND clear
 * its power-down bit. The raw ROM Pinmux_Config alone left PB1/PB2 powered down on
 * the LP pads, so UART3 TX never reached the pin. (See src/UartAmebaGlue -> SDK
 * serial_api -> pin_function.) */
static void pad_uart_func(u8 pin)
{
    u32 t = PINMUX->PADCTR[pin & 0x3F];
    t &= ~PAD_BIT_MASK_FUNCTION_ID;
    t |= (PINMUX_FUNCTION_UART & PAD_BIT_MASK_FUNCTION_ID);
    t &= ~PAD_BIT_SHUT_DWON;            /* power the pad up */
    PINMUX->PADCTR[pin & 0x3F] = t;
}

void amebad_console_init_lpuart(void)
{
    UART_InitTypeDef u;

    pad_uart_func(LPUART_TX_PIN);
    pad_uart_func(LPUART_RX_PIN);
    PAD_PullCtrl(LPUART_TX_PIN, GPIO_PuPd_UP);
    PAD_PullCtrl(LPUART_RX_PIN, GPIO_PuPd_UP);

    UART_StructInit(&u);                          /* defaults: 8 data bits, 1 stop, no parity */
    UART_Init(LPUART_DEV, &u);
    /* Select the 40 MHz XTAL as the UART clock source before computing the baud
     * divisor — without this UART_SetBaud divides against the wrong (LP osc)
     * clock and nothing usable comes out. Mirrors the SDK's serial_baud(). */
    RCC_PeriphClockSource_UART(LPUART_DEV, UART_RX_CLK_XTAL_40M);
    UART_SetBaud(LPUART_DEV, LPUART_BAUD);
    UART_RxCmd(LPUART_DEV, ENABLE);

    /* DIAG: is UART3 actually clocked? If the LP-domain LUART clock is off, the
     * TX FIFO status never goes writable (status reads as 0). */
    DiagPrintf("[LPUART] init done: writable=%d LSR=0x%x\n\r",
               (int)UART_Writable(LPUART_DEV),
               (unsigned)UART_LineStatusGet(LPUART_DEV));
    /* DIAG: LP-domain clock/function-enable regs (0x48000000 base):
     * SYS_CLK_CTRL1 @0x214 (UARTx CKE bits), SYS_FUNC_EN1 @0x20C (UARTx FEN). */
    DiagPrintf("[LPUART] LP CLK_CTRL1=0x%x FUNC_EN1=0x%x PADCTR[B1]=0x%x PADCTR[B2]=0x%x\n\r",
               (unsigned)HAL_READ32(0x48000000, 0x214),
               (unsigned)HAL_READ32(0x48000000, 0x20C),
               (unsigned)PINMUX->PADCTR[LPUART_TX_PIN & 0x3F],
               (unsigned)PINMUX->PADCTR[LPUART_RX_PIN & 0x3F]);

    /* DIAG: blast a known string straight out UART3 TX so we can tell on COM7
     * whether the pin/baud are right, independent of the C++ iostream path. */
    {
        const char *m = "LPUART-DIRECT-HELLO\r\n";
        const char *p;
        for (p = m; *p; ++p) { lp_putc(*p); }
    }

    at_console_set_backend(&LPUART_BACKEND);
}
