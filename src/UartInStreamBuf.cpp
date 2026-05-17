#include "Uart.h"

#include "UartInStreamBuf.h"
#include "UartDispatch.h"

#if defined AT_UART_BACKEND_BFLB
#  include <bflb_uart.h>
#elif defined AT_UART_BACKEND_AMEBA
#  include "UartAmebaGlue.h"
#endif

namespace at {

UartInStreamBuf::UartInStreamBuf(UartDevice *dev)
    : m_dev(dev), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setg(m_getBuf, m_getBuf, m_getBuf);
    UartDispatch::registerIn(m_dev, this);
#if defined AT_UART_BACKEND_BFLB
    bflb_uart_rxint_mask(m_dev, false);
#elif defined AT_UART_BACKEND_AMEBA
    at_uart_irq_set(m_dev, AT_UART_IRQ_RX, 1);
#endif
}

UartInStreamBuf::~UartInStreamBuf() {
#if defined AT_UART_BACKEND_BFLB
    bflb_uart_rxint_mask(m_dev, true);
#elif defined AT_UART_BACKEND_AMEBA
    at_uart_irq_set(m_dev, AT_UART_IRQ_RX, 0);
#endif
    UartDispatch::unregisterIn(m_dev);
    vStreamBufferDelete(m_stream);
}

void UartInStreamBuf::onRxIsr(BaseType_t &woken) {
#if defined AT_UART_BACKEND_BFLB
    int b;
    while ((b = bflb_uart_getchar(m_dev)) >= 0) {
        uint8_t byte = static_cast<uint8_t>(b);
        xStreamBufferSendFromISR(m_stream, &byte, 1, &woken);
    }
#elif defined AT_UART_BACKEND_AMEBA
    // serial_getc() busy-waits on serial_readable(); we MUST gate on
    // at_uart_readable() here or the ISR will hang once the FIFO is empty.
    while (at_uart_readable(m_dev)) {
        uint8_t byte = static_cast<uint8_t>(at_uart_getc(m_dev));
        xStreamBufferSendFromISR(m_stream, &byte, 1, &woken);
    }
#endif
}

int UartInStreamBuf::underflow() {
    size_t got = xStreamBufferReceive(m_stream, m_getBuf, kGetBufSize, portMAX_DELAY);
    if (got == 0) return traits_type::eof();
    setg(m_getBuf, m_getBuf, m_getBuf + got);
    return traits_type::to_int_type(m_getBuf[0]);
}

std::streamsize UartInStreamBuf::xsgetn(char_type *s, std::streamsize n) {
    std::streamsize total = 0;
    std::streamsize avail = egptr() - gptr();
    if (avail > 0) {
        std::streamsize take = (avail < n) ? avail : n;
        traits_type::copy(s, gptr(), take);
        gbump(take);
        s += take;
        total += take;
    }
    while (total < n) {
        size_t got = xStreamBufferReceive(m_stream, s, static_cast<size_t>(n - total), portMAX_DELAY);
        if (got == 0) break;
        s += got;
        total += static_cast<std::streamsize>(got);
    }
    return total;
}

}  // namespace at
