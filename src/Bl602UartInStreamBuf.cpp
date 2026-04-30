#if defined BL602 || defined BL702

#include "Bl602UartInStreamBuf.h"
extern "C" {
#include <bl_uart.h>
}

namespace at {

Bl602UartInStreamBuf::Bl602UartInStreamBuf(uint8_t uartId)
    : m_id(uartId), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setg(m_getBuf, m_getBuf, m_getBuf);
    bl_uart_int_rx_notify_register(m_id, rxIsr, this);
    // bl_uart_init() 不安装 UART IRQ 向量；调用 bl_uart_int_enable() 注册
    // UARTx_IRQHandler 并使能中断分发，再打开 RX 掩码。
    bl_uart_int_enable(m_id);
    bl_uart_int_rx_enable(m_id);
}

Bl602UartInStreamBuf::~Bl602UartInStreamBuf() {
    bl_uart_int_rx_disable(m_id);
    bl_uart_int_rx_notify_unregister(m_id, rxIsr, this);
    vStreamBufferDelete(m_stream);
}

// Called from RX interrupt: drain the UART RX FIFO into the StreamBuffer.
// bl_uart_data_recv() returns -1 when the FIFO is empty, so loop until then.
void Bl602UartInStreamBuf::rxIsr(void *arg) {
    auto      *self  = static_cast<Bl602UartInStreamBuf *>(arg);
    BaseType_t woken = pdFALSE;
    int        b;
    while ((b = bl_uart_data_recv(self->m_id)) >= 0) {
        uint8_t byte = static_cast<uint8_t>(b);
        xStreamBufferSendFromISR(self->m_stream, &byte, 1, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

int Bl602UartInStreamBuf::underflow() {
    size_t got = xStreamBufferReceive(m_stream, m_getBuf, kGetBufSize, portMAX_DELAY);
    if (got == 0) return traits_type::eof();
    setg(m_getBuf, m_getBuf, m_getBuf + got);
    return traits_type::to_int_type(m_getBuf[0]);
}

std::streamsize Bl602UartInStreamBuf::xsgetn(char_type *s, std::streamsize n) {
    std::streamsize total = 0;
    // Drain whatever is already in the get-area first.
    std::streamsize avail = egptr() - gptr();
    if (avail > 0) {
        std::streamsize take = (avail < n) ? avail : n;
        traits_type::copy(s, gptr(), take);
        gbump(take);
        s += take;
        total += take;
    }
    // Block in StreamBuffer for the remainder.
    while (total < n) {
        size_t got = xStreamBufferReceive(m_stream, s, static_cast<size_t>(n - total), portMAX_DELAY);
        if (got == 0) break;
        s += got;
        total += static_cast<std::streamsize>(got);
    }
    return total;
}

}  // namespace at

#endif // defined BL602 || defined BL702
