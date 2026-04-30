#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

namespace at {

class Bl602UartOutStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kPutBufSize    = 64;

    explicit Bl602UartOutStreamBuf(uint8_t uartId);
    ~Bl602UartOutStreamBuf();

protected:
    int             overflow(int c) override;
    int             sync() override;
    std::streamsize xsputn(const char_type *s, std::streamsize n) override;

private:
    uint8_t              m_id;
    StreamBufferHandle_t m_stream;
    char                 m_putBuf[kPutBufSize];

    static void txIsr(void *arg);
    int         flushPutArea();
};

}  // namespace at

#endif // defined BL602 || defined BL702
