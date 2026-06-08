/*
 * RTL8721D KM4 — TF-M device config (minimal).
 * Only the bits TF-M's common uart_stdout.c needs; the Ameba LOGUART is already
 * initialised by the boot chain, so Driver_USART0 ignores baud/control.
 */
#ifndef __DEVICE_CFG_H__
#define __DEVICE_CFG_H__

#define DEFAULT_UART_CONTROL    0
#define DEFAULT_UART_BAUDRATE   1500000   /* Ameba LOGUART runtime baud */

#endif /* __DEVICE_CFG_H__ */
