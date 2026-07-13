/*
 * component/bouffalo_sdk/syscall_port — Bouffalo's console backend for the
 * cross-platform newlib retarget. The retarget core (POSIX fd VFS + malloc +
 * env + time + sockets) is the shared component/syscall_config; what stays here
 * is the platform-specific byte I/O: the bflb_uart console, registered as the
 * strong at_console_default_backend for the shared dispatcher (component/console).
 *
 * Extracted from the old bflb_libc/newlib/syscalls_simple_io.c (its _read_r/
 * _write_r retarget is dropped — syscall_config owns fd 0/1/2 now).
 */
#include <stdarg.h>
#include <stdio.h>     /* vsnprintf (malloc-free for the %x DiagPrintf below) */

#ifdef CONFIG_CONSOLE_WO
#include "bflb_wo.h"
#else
#include "bflb_uart.h"
#include "bflb_irq.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#endif

#include "console.h"   /* cross-platform fd 0/1/2 dispatcher (component/console) */

struct bflb_device_s *console = NULL;

#ifndef CONFIG_CONSOLE_WO
/* IRQ-driven console RX (mirrors the AmebaD KM4 monitor_cli path): the ISR drains the whole HW
 * RX FIFO into a StreamBuffer, so a fast paste (e.g. a ~380 B `ota_http <presigned-URL>` line at
 * 2 Mbps) never overruns the small UART FIFO the way the old getchar-polling console did (the
 * monitor RX loop polls every ~10 ms; ~2000 bytes arrive in that window at 2 Mbps -> dropped
 * chars, corrupt command). getc() then just pops one byte from the SW FIFO. */
#define CONSOLE_RX_STREAM_SZ   1024
static StreamBufferHandle_t s_rx_stream;

/* Cast at bflb_irq_attach() time (as src/main.cpp does for the WiFi ISR): the SDK's
 * irq_callback is void(int,void*) or void(void*) depending on the build, so keep the
 * handler parameterless and cast. */
static void bflb_console_rx_isr(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t sts = bflb_uart_get_intstatus(console);

    if (sts & (UART_INTSTS_RX_FIFO | UART_INTSTS_RTO)) {
        while (bflb_uart_rxavailable(console)) {
            uint8_t c = (uint8_t) bflb_uart_getchar(console);   /* reading drains the FIFO -> deasserts RX_FIFO */
            if (s_rx_stream != NULL) {
                ( void ) xStreamBufferSendFromISR(s_rx_stream, &c, 1, &woken);
            }
        }
        bflb_uart_int_clear(console, UART_INTCLR_RTO);   /* RX_FIFO is level (cleared by draining); RTO is latched */
    }
    portYIELD_FROM_ISR(woken);
}

/* Install the RX ISR once the console UART device is known. Idempotent-ish: called from
 * bflb_uart_set_console() right after the SDK hands us the configured device. */
static void bflb_console_rx_irq_init(void)
{
    if (console == NULL || s_rx_stream != NULL) {
        return;
    }
    s_rx_stream = xStreamBufferCreate(CONSOLE_RX_STREAM_SZ, 1);
    if (s_rx_stream == NULL) {
        return;   /* falls back to the polling getc below */
    }
    bflb_irq_attach(console->irq_num, (irq_callback) bflb_console_rx_isr, NULL);
    bflb_uart_rxint_mask(console, false);   /* unmask RX FIFO + timeout interrupt */
    bflb_irq_enable(console->irq_num);
}
#endif /* !CONFIG_CONSOLE_WO */

#ifdef CONFIG_CONSOLE_WO
void bflb_wo_set_console(struct bflb_device_s *dev)
{
    console = dev;
}
#else
void bflb_uart_set_console(struct bflb_device_s *dev)
{
    console = dev;
    bflb_console_rx_irq_init();   /* IRQ-driven RX once the device is known */
}
#endif

#ifdef CONFIG_CONSOLE_WO
    #define CONSOLE_PUTCHAR(console, x) bflb_wo_uart_putchar(console, x)
#else
    #define CONSOLE_PUTCHAR(console, x) bflb_uart_putchar(console, x)
#endif

/* Bouffalo console backend for the shared dispatcher: bytes go to the global
 * `console` UART device. putc keeps the SDK's '\n' -> "\r\n" expansion so log
 * output formatting is unchanged. */
static void bflb_console_putc(char c)
{
    if (c == '\n') {
        CONSOLE_PUTCHAR(console, '\r');
    }
    CONSOLE_PUTCHAR(console, c);
}

#ifndef CONFIG_CONSOLE_WO
static int bflb_console_getc(void)
{
    /* Pop one byte from the IRQ-fed SW FIFO (non-blocking: the monitor RX loop expects <0 when
     * idle and adds its own vTaskDelay). Falls back to a direct HW read before the ISR is armed. */
    if (s_rx_stream != NULL) {
        uint8_t c;
        if (xStreamBufferReceive(s_rx_stream, &c, 1, 0) == 1) {
            return (int) c;
        }
        return -1;
    }
    return bflb_uart_getchar(console);   /* pre-init fallback; <0 if no input */
}
#endif

static const at_console_backend_t BFLB_CONSOLE = {
    bflb_console_putc,                   /* putc    */
#ifndef CONFIG_CONSOLE_WO
    bflb_console_getc,                   /* getc    */
#else
    0,                                   /* write-only console (WO) */
#endif
    bflb_console_putc,                   /* errputc -> same console */
};

/* Strong override of the dispatcher's weak default: zero-config console. */
const at_console_backend_t *at_console_default_backend(void)
{
    return &BFLB_CONSOLE;
}

/* AmebaD names its ROM console print DiagPrintf; component/syscall_config's malloc
 * uses it for the OOM/overflow diagnostic — deliberately NOT printf, since that path
 * runs inside malloc() and newlib printf can itself allocate. Provide the Bouffalo
 * DiagPrintf here: vsnprintf into a stack buffer (malloc-free for the %x it uses) +
 * a direct uart write, so it allocates nothing. AmebaD gets DiagPrintf from ROM. */
void DiagPrintf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1;
    }
    at_console_write(2, buf, n);   /* fd 2 = stderr; direct to the console backend */
}
