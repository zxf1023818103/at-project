/*
 * console_lpuart.c — route stdin/stdout to the LPUART (UART3, PB1 TX / PB2 RX),
 * leaving stderr on the LOGUART.
 *
 * The console defaults to the LOGUART (console_loguart.c). Calling
 * amebad_console_init_lpuart() once at startup brings up UART3 via the fwlib UART
 * HAL (all ROM _LONG_CALL_ entries — no extra driver TU) and registers an LPUART
 * backend with the shared console dispatcher (component/console,
 * at_console_set_backend), so after:
 *
 *   stdin/stdout (fd 0/1)  -> LPUART  (host COM7)        -> std::cin / std::cout
 *   stderr       (fd 2)    -> LOGUART (the log console)  -> std::cerr / DiagPrintf
 *
 * Line control: UART_Init's LCR encoding for UART3 does NOT match the mbed
 * serial_api/serial_format encoding that the proven (since-deleted) AT-UART path
 * and the BW16 combo HAL use on this exact pin pair. We therefore write LCR by hand,
 * byte-identically to serial_format(8, ParityNone, 1):
 *     LCR = WordLen | (StopBit<<2) | (ParityEn<<3) | (ParityType<<4) | (Stick<<5)
 * For 8N1 that is RUART_WLS_8BITS = 0x01. (UART_StructInit's defaults yielded LCR
 * 0x0b = parity-enabled + an extra word-length bit, which an 8N1 host silently
 * dropped — "nothing on the wire" despite the transmitter shifting fine.)
 *
 * Gated into the build (AMEBAD_CPP_SELFTEST). KM4-only.
 */
#include "ameba_soc.h"       /* fwlib UART HAL + Pinmux_Config + UART3_DEV + _PB_1/_PB_2 */
#include "console.h"         /* at_console_set_backend + at_console_backend_t */

/* PB1=TX / PB2=RX -> UART3 (LPUART). 115200 8N1 to match the COM7 host setup. */
#define LPUART_DEV      UART3_DEV
#define LPUART_TX_PIN   _PB_1
#define LPUART_RX_PIN   _PB_2
#define LPUART_BAUD     115200

/* 8N1 line control, encoded exactly like mbed serial_format() (the proven path):
 * word-length is a single bit (RUART_WLS_8BITS=1), parity disabled, 1 stop. */
#define LPUART_LCR_8N1  (RUART_WLS_8BITS \
                         | (RUART_STOP_BIT_1   << 2) \
                         | (RUART_PARITY_DISABLE << 3) \
                         | (RUART_ODD_PARITY   << 4) \
                         | (RUART_STICK_PARITY_DISABLE << 5))

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
 * the LP pads, so UART3 TX never reached the pin. (See the old AT-UART glue -> SDK
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

    UART_StructInit(&u);
    UART_Init(LPUART_DEV, &u);
    /* Select the 40 MHz XTAL as the UART clock source before computing the baud
     * divisor — without this UART_SetBaud divides against the wrong (LP osc)
     * clock and nothing usable comes out. Mirrors the SDK's serial_baud(). */
    RCC_PeriphClockSource_UART(LPUART_DEV, UART_RX_CLK_XTAL_40M);
    UART_SetBaud(LPUART_DEV, LPUART_BAUD);
    /* Override the line control to serial_format's exact 8N1 encoding (UART_Init's
     * default LCR encoding for UART3 differs and the host rejects those frames). */
    ((UART_TypeDef *)LPUART_DEV)->LCR = LPUART_LCR_8N1;
    UART_RxCmd(LPUART_DEV, ENABLE);

    /* DIAG: confirm clock + the resulting line control (want LCR=0x1). */
    DiagPrintf("[LPUART] init done: writable=%d LCR=0x%x LSR=0x%x\n\r",
               (int)UART_Writable(LPUART_DEV),
               (unsigned)((UART_TypeDef *)LPUART_DEV)->LCR,
               (unsigned)UART_LineStatusGet(LPUART_DEV));

    /* DIAG: blast a known string straight out UART3 TX so we can tell on COM7
     * whether the pin/baud/format are right, independent of the iostream path. */
    {
        const char *m = "LPUART-DIRECT-HELLO\r\n";
        const char *p;
        for (p = m; *p; ++p) { lp_putc(*p); }
    }

    at_console_set_backend(&LPUART_BACKEND);
}
