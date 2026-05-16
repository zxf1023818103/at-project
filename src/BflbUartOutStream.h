#pragma once
#if defined BL602 || defined BL702

#include <ostream>

#include "BflbUartOutStreamBuf.h"

namespace at {

class BflbUartOutStream : public std::ostream {
public:
    explicit BflbUartOutStream(struct bflb_device_s *dev);

private:
    BflbUartOutStreamBuf m_buf;
};

}  // namespace at

#endif // defined BL602 || defined BL702
