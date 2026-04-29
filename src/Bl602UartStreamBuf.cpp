#if defined BL602 || defined BL702

#include "Bl602UartStreamBuf.h"
extern "C" {
#include <bl_uart.h>
}

namespace at {

Bl602UartStreamBuf::Bl602UartStreamBuf(uint8_t uartId)
    : m_id(uartId), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setp(m_putBuf, m_putBuf + kPutBufSize);
    bl_uart_int_tx_notify_register(m_id, txIsr, this);
}

Bl602UartStreamBuf::~Bl602UartStreamBuf() {
    sync();
    bl_uart_int_tx_disable(m_id);
    bl_uart_int_tx_notify_unregister(m_id, txIsr, this);
    vStreamBufferDelete(m_stream);
}

// Called from TX interrupt: feeds exactly one byte to the UART per call,
// relying on the hardware to fire the next interrupt when the FIFO is ready.
// On empty, the disable+recheck pair closes the race with a producer that
// may have written and re-enabled the interrupt during the read.
void Bl602UartStreamBuf::txIsr(void *arg) {
    auto      *self  = static_cast<Bl602UartStreamBuf *>(arg);
    BaseType_t woken = pdFALSE;
    uint8_t    byte;
    if (xStreamBufferReceiveFromISR(self->m_stream, &byte, 1, &woken) == 1) {
        bl_uart_data_send(self->m_id, byte);
    } else {
        bl_uart_int_tx_disable(self->m_id);
        if (xStreamBufferIsEmpty(self->m_stream) == pdFALSE) {
            bl_uart_int_tx_enable(self->m_id);
        }
    }
    portYIELD_FROM_ISR(woken);
}

int Bl602UartStreamBuf::flushPutArea() {
    char  *base = pbase();
    size_t len  = static_cast<size_t>(pptr() - base);
    if (len == 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, base, len, portMAX_DELAY);
    setp(base, epptr());
    bl_uart_int_tx_enable(m_id);
    return (sent == len) ? 0 : -1;
}

int Bl602UartStreamBuf::overflow(int c) {
    if (flushPutArea() != 0) return traits_type::eof();
    if (!traits_type::eq_int_type(c, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(c);
        pbump(1);
    }
    return traits_type::not_eof(c);
}

int Bl602UartStreamBuf::sync() {
    return flushPutArea();
}

std::streamsize Bl602UartStreamBuf::xsputn(const char_type *s, std::streamsize n) {
    if (flushPutArea() != 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, s, static_cast<size_t>(n), portMAX_DELAY);
    if (sent > 0) {
        bl_uart_int_tx_enable(m_id);
    }
    return static_cast<std::streamsize>(sent);
}

Bl602UartOutStream::Bl602UartOutStream(uint8_t uartId)
    : std::ostream(nullptr), m_buf(uartId) {
    rdbuf(&m_buf);
}

}  // namespace at

#endif // defined BL602 || defined BL702
