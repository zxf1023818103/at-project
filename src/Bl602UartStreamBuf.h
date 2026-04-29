#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <ostream>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

namespace at {

class Bl602UartStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kPutBufSize    = 64;

    explicit Bl602UartStreamBuf(uint8_t uartId);
    ~Bl602UartStreamBuf();

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

class Bl602UartOutStream : public std::ostream {
public:
    explicit Bl602UartOutStream(uint8_t uartId);

private:
    Bl602UartStreamBuf m_buf;
};

}  // namespace at

#endif // defined BL602 || defined BL702
