#pragma once
#include "Uart.h"

#include <cstdint>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

namespace at {

class UartInStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kGetBufSize    = 64;

    explicit UartInStreamBuf(UartDevice *dev);
    ~UartInStreamBuf();

    // Called by the shared UART ISR (UartDispatch). Drains the UART RX FIFO
    // into the StreamBuffer.
    void onRxIsr(BaseType_t &woken);

protected:
    int             underflow() override;
    std::streamsize xsgetn(char_type *s, std::streamsize n) override;

private:
    UartDevice          *m_dev;
    StreamBufferHandle_t m_stream;
    char                 m_getBuf[kGetBufSize];
};

}  // namespace at
