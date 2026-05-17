#include "Uart.h"

#if defined AT_UART_BACKEND_AMEBA

#include "UartAmebaGlue.h"
#include "serial_api.h"

// Small static pool keeps every serial_t pointer stable for the lifetime of
// the program; at_uart_create hands an opaque pointer to the caller.
#define AT_UART_POOL_SIZE 4
static serial_t s_pool[AT_UART_POOL_SIZE];
static int      s_pool_used;

at_uart_handle_t *at_uart_create(int tx_pin, int rx_pin, int baudrate) {
    if (s_pool_used >= AT_UART_POOL_SIZE) {
        return (at_uart_handle_t *)0;
    }
    serial_t *obj = &s_pool[s_pool_used++];
    serial_init(obj, (PinName)tx_pin, (PinName)rx_pin);
    serial_baud(obj, baudrate);
    serial_format(obj, 8, ParityNone, 1);
    return (at_uart_handle_t *)obj;
}

void at_uart_irq_handler_install(at_uart_handle_t *u,
                                 at_uart_irq_handler_t h,
                                 uint32_t id) {
    // SerialIrq and at_uart_irq_t share the same numeric layout (RxIrq=0,
    // TxIrq=1). ABI-identical signatures, so the function pointer cast is
    // safe in practice on this toolchain.
    serial_irq_handler((serial_t *)u, (uart_irq_handler)(void (*)(void))h, id);
}

void at_uart_irq_set(at_uart_handle_t *u, at_uart_irq_t irq, int enable) {
    serial_irq_set((serial_t *)u, (SerialIrq)irq, enable);
}

int at_uart_readable(at_uart_handle_t *u) {
    return serial_readable((serial_t *)u);
}

int at_uart_getc(at_uart_handle_t *u) {
    return serial_getc((serial_t *)u);
}

void at_uart_putc(at_uart_handle_t *u, int c) {
    serial_putc((serial_t *)u, c);
}

#endif
