#if defined BL602 || defined BL702

#include "BflbUartOutStreamBuf.h"
#include "BflbUartDispatch.h"

#include <bflb_uart.h>

namespace at {

BflbUartOutStreamBuf::BflbUartOutStreamBuf(struct bflb_device_s *dev)
    : m_dev(dev), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setp(m_putBuf, m_putBuf + kPutBufSize);
    BflbUartDispatch::registerOut(m_dev, this);
    bflb_uart_txint_mask(m_dev, true);
}

BflbUartOutStreamBuf::~BflbUartOutStreamBuf() {
    sync();
    bflb_uart_txint_mask(m_dev, true);
    BflbUartDispatch::unregisterOut(m_dev);
    vStreamBufferDelete(m_stream);
}

void BflbUartOutStreamBuf::onTxIsr(BaseType_t &woken) {
    uint8_t byte;
    if (xStreamBufferReceiveFromISR(m_stream, &byte, 1, &woken) == 1) {
        bflb_uart_putchar(m_dev, byte);
    } else {
        bflb_uart_txint_mask(m_dev, true);
        if (xStreamBufferIsEmpty(m_stream) == pdFALSE) {
            bflb_uart_txint_mask(m_dev, false);
        }
    }
}

int BflbUartOutStreamBuf::flushPutArea() {
    char  *base = pbase();
    size_t len  = static_cast<size_t>(pptr() - base);
    if (len == 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, base, len, portMAX_DELAY);
    setp(base, epptr());
    bflb_uart_txint_mask(m_dev, false);
    return (sent == len) ? 0 : -1;
}

int BflbUartOutStreamBuf::overflow(int c) {
    if (flushPutArea() != 0) return traits_type::eof();
    if (!traits_type::eq_int_type(c, traits_type::eof())) {
        *pptr() = traits_type::to_char_type(c);
        pbump(1);
    }
    return traits_type::not_eof(c);
}

int BflbUartOutStreamBuf::sync() {
    return flushPutArea();
}

std::streamsize BflbUartOutStreamBuf::xsputn(const char_type *s, std::streamsize n) {
    if (flushPutArea() != 0) return 0;
    size_t sent = xStreamBufferSend(m_stream, s, static_cast<size_t>(n), portMAX_DELAY);
    if (sent > 0) {
        bflb_uart_txint_mask(m_dev, false);
    }
    return static_cast<std::streamsize>(sent);
}

}  // namespace at

#endif // defined BL602 || defined BL702
