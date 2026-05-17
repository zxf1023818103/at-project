#include "UartOutStream.h"

namespace at {

UartOutStream::UartOutStream(UartDevice *dev)
    : std::ostream(nullptr), m_buf(dev) {
    rdbuf(&m_buf);
}

}  // namespace at
