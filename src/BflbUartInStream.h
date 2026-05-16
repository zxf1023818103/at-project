#pragma once
#if defined BL602 || defined BL702

#include <istream>

#include "BflbUartInStreamBuf.h"

namespace at {

class BflbUartInStream : public std::istream {
public:
    explicit BflbUartInStream(struct bflb_device_s *dev);

private:
    BflbUartInStreamBuf m_buf;
};

}  // namespace at

#endif // defined BL602 || defined BL702
