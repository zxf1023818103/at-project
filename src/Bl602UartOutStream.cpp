#if defined BL602 || defined BL702

#include "Bl602UartOutStream.h"

namespace at {

Bl602UartOutStream::Bl602UartOutStream(uint8_t uartId)
    : std::ostream(nullptr), m_buf(uartId) {
    rdbuf(&m_buf);
}

}  // namespace at

#endif // defined BL602 || defined BL702
