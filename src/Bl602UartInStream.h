#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <istream>

#include "Bl602UartInStreamBuf.h"

namespace at {

class Bl602UartInStream : public std::istream {
public:
    explicit Bl602UartInStream(uint8_t uartId);

private:
    Bl602UartInStreamBuf m_buf;
};

}  // namespace at

#endif // defined BL602 || defined BL702
