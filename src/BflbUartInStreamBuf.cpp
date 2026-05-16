#if defined BL602 || defined BL702

#include "BflbUartInStreamBuf.h"
#include "BflbUartDispatch.h"

#include <bflb_uart.h>

namespace at {

BflbUartInStreamBuf::BflbUartInStreamBuf(struct bflb_device_s *dev)
    : m_dev(dev), m_stream(xStreamBufferCreate(kStreamBufSize, 1)) {
    setg(m_getBuf, m_getBuf, m_getBuf);
    BflbUartDispatch::registerIn(m_dev, this);
    bflb_uart_rxint_mask(m_dev, false);
}

BflbUartInStreamBuf::~BflbUartInStreamBuf() {
    bflb_uart_rxint_mask(m_dev, true);
    BflbUartDispatch::unregisterIn(m_dev);
    vStreamBufferDelete(m_stream);
}

void BflbUartInStreamBuf::onRxIsr(BaseType_t &woken) {
    int b;
    while ((b = bflb_uart_getchar(m_dev)) >= 0) {
        uint8_t byte = static_cast<uint8_t>(b);
        xStreamBufferSendFromISR(m_stream, &byte, 1, &woken);
    }
}

int BflbUartInStreamBuf::underflow() {
    size_t got = xStreamBufferReceive(m_stream, m_getBuf, kGetBufSize, portMAX_DELAY);
    if (got == 0) return traits_type::eof();
    setg(m_getBuf, m_getBuf, m_getBuf + got);
    return traits_type::to_int_type(m_getBuf[0]);
}

std::streamsize BflbUartInStreamBuf::xsgetn(char_type *s, std::streamsize n) {
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

#endif // defined BL602 || defined BL702
