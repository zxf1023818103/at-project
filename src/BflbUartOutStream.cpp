#if defined BL602 || defined BL702

#include "BflbUartOutStream.h"

namespace at {

BflbUartOutStream::BflbUartOutStream(struct bflb_device_s *dev)
    : std::ostream(nullptr), m_buf(dev) {
    rdbuf(&m_buf);
}

}  // namespace at

#endif // defined BL602 || defined BL702
