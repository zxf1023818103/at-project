#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <streambuf>

#include <FreeRTOS.h>
#include <stream_buffer.h>

struct bflb_device_s;

namespace at {

class BflbUartInStreamBuf : public std::streambuf {
public:
    static constexpr size_t kStreamBufSize = 256;
    static constexpr size_t kGetBufSize    = 64;

    explicit BflbUartInStreamBuf(struct bflb_device_s *dev);
    ~BflbUartInStreamBuf();

    // Called by the shared UART ISR (BflbUartDispatch). Drains the UART RX FIFO
    // into the StreamBuffer; the dispatch handles int-status decode and clear.
    void onRxIsr(BaseType_t &woken);

protected:
    int             underflow() override;
    std::streamsize xsgetn(char_type *s, std::streamsize n) override;

private:
    struct bflb_device_s *m_dev;
    StreamBufferHandle_t  m_stream;
    char                  m_getBuf[kGetBufSize];
};

}  // namespace at

#endif // defined BL602 || defined BL702
