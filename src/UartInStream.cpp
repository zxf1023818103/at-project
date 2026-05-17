#include "UartInStream.h"

namespace at {

UartInStream::UartInStream(UartDevice *dev)
    : std::istream(nullptr), m_buf(dev) {
    rdbuf(&m_buf);
}

}  // namespace at
