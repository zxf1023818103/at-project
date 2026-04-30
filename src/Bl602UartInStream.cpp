#if defined BL602 || defined BL702

#include "Bl602UartInStream.h"

namespace at {

Bl602UartInStream::Bl602UartInStream(uint8_t uartId)
    : std::istream(nullptr), m_buf(uartId) {
    rdbuf(&m_buf);
}

}  // namespace at

#endif // defined BL602 || defined BL702
