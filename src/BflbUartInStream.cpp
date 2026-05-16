#if defined BL602 || defined BL702

#include "BflbUartInStream.h"

namespace at {

BflbUartInStream::BflbUartInStream(struct bflb_device_s *dev)
    : std::istream(nullptr), m_buf(dev) {
    rdbuf(&m_buf);
}

}  // namespace at

#endif // defined BL602 || defined BL702
