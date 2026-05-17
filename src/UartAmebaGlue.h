#pragma once

// C-callable wrapper over the Ameba mbed serial_api so C++ TUs can avoid
// including serial_api.h directly. The amebagreen2 SDK's PeripheralNames.h
// (transitively pulled by serial_api.h) uses `(int)POINTER_LITERAL` enum
// initializers that g++ rejects as non-constexpr. Wrapping in a .c TU
// sidesteps the issue across all Ameba targets uniformly.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AT_UART_IRQ_RX = 0,
    AT_UART_IRQ_TX = 1
} at_uart_irq_t;

// Opaque handle (SDK serial_t * under the hood).
typedef struct at_uart_handle_s at_uart_handle_t;

typedef void (*at_uart_irq_handler_t)(uint32_t id, at_uart_irq_t irq);

at_uart_handle_t *at_uart_create(int tx_pin, int rx_pin, int baudrate);
void              at_uart_irq_handler_install(at_uart_handle_t *u,
                                              at_uart_irq_handler_t h,
                                              uint32_t id);
void              at_uart_irq_set(at_uart_handle_t *u, at_uart_irq_t irq, int enable);
int               at_uart_readable(at_uart_handle_t *u);
int               at_uart_getc(at_uart_handle_t *u);
void              at_uart_putc(at_uart_handle_t *u, int c);

#ifdef __cplusplus
}  // extern "C"
#endif
