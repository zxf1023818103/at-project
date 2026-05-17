#include "Uart.h"

#include "UartOutStreamBuf.h"
#include "UartDispatch.h"

#if defined AT_UART_BACKEND_BFLB
#  include <bflb_uart.h>
#elif defined AT_UART_BACKEND_AMEBA
#  include "UartAmebaGlue.h"
#endif

namespace at {

namespace {

inline void enable_tx_irq(UartDevice *dev) {
#if defined AT_UART_BACKEND_BFLB
    bflb_uart_txint_mask(dev, false);
#elif defined AT_UART_BACKEND_AMEBA
    at_uart_irq_set(dev, AT_UART_IRQ_TX, 1);
#endif
}

inline void disable_tx_irq(UartDevice *dev) {
#if defined AT_UART_BACKEND_BFLB
    bflb_uart_txint_mask(dev, true);
#elif defined AT_UART_BACKEND_AMEBA
    at_uart_irq_set(dev, AT_UART_IRQ_TX, 0);
#endif
}

inline void write_byte(UartDevice *dev, uint8_t byte) {
#if defined AT_UART_BACKEND_BFLB
    bflb_uart_putchar(dev, byte);
#elif defined AT_UART_BACKEND_AMEBA
    at_uart_putc(dev, byte);
#endif
}

}  // namespace

UartOutStreamBuf::UartOutStreamBuf(UartDevice *dev)
    : m_dev(dev), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setp(m_putBuf, m_putBuf + kPutBufSize);
    UartDispatch::registerOut(m_dev, this);
    disable_tx_irq(m_dev);
}

UartOutStreamBuf::~UartOutStreamBuf() {
    sync();
    disable_tx_irq(m_dev);
    UartDispatch::unregisterOut(m_dev);
    vStreamBufferDelete(m_stream);
}

void UartOutStreamBuf::onTxIsr(BaseType_t &woken) {
    uint8_t byte;
    if (xStreamBufferReceiveFromISR(m_stream, &byte, 1, &woken) == 1) {
        write_byte(m_dev, byte);
    } else {
        disable_tx_irq(m_dev);
        if (xStreamBufferIsEmpty(m_stream) == pdFALSE) {
            enable_tx_irq(m_dev);
        }
    }
}

int UartOutStreamBuf::flushPutArea() {
    char  *base = pbase();
    size_t len  = static_cast<size_t>(pptr() - base);
    if (len == 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, base, len, portMAX_DELAY);
    setp(base, epptr());
    enable_tx_irq(m_dev);
    return (sent == len) ? 0 : -1;
}

int UartOutStreamBuf::overflow(int c) {
    if (flushPutArea() != 0) return traits_type::eof();
    if (!traits_type::eq_int_type(c, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(c);
        pbump(1);
    }
    return traits_type::not_eof(c);
}

int UartOutStreamBuf::sync() {
    return flushPutArea();
}

std::streamsize UartOutStreamBuf::xsputn(const char_type *s, std::streamsize n) {
    if (flushPutArea() != 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, s, static_cast<size_t>(n), portMAX_DELAY);
    if (sent > 0) {
        enable_tx_irq(m_dev);
    }
    return static_cast<std::streamsize>(sent);
}

}  // namespace at
