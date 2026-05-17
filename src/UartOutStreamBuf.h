#pragma once
#include "Uart.h"

#include <cstdint>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

namespace at {

class UartOutStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kPutBufSize    = 64;

    explicit UartOutStreamBuf(UartDevice *dev);
    ~UartOutStreamBuf();

    // Called by the shared UART ISR (UartDispatch). Pops one byte from the
    // StreamBuffer and pushes it to the UART TX FIFO; disables TX interrupt
    // when the buffer drains.
    void onTxIsr(BaseType_t &woken);

protected:
    int             overflow(int c) override;
    int             sync() override;
    std::streamsize xsputn(const char_type *s, std::streamsize n) override;

private:
    UartDevice          *m_dev;
    StreamBufferHandle_t m_stream;
    char                 m_putBuf[kPutBufSize];

    int flushPutArea();
};

}  // namespace at
