#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

namespace at {

class Bl602UartInStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kGetBufSize    = 64;

    explicit Bl602UartInStreamBuf(uint8_t uartId);
    ~Bl602UartInStreamBuf();

protected:
    int             underflow() override;
    std::streamsize xsgetn(char_type *s, std::streamsize n) override;

private:
    uint8_t              m_id;
    StreamBufferHandle_t m_stream;
    char                 m_getBuf[kGetBufSize];

    static void rxIsr(void *arg);
};

}  // namespace at

#endif // defined BL602 || defined BL702
