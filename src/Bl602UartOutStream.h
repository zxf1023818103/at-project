#pragma once
#if defined BL602 || defined BL702

#include <cstdint>
#include <ostream>

#include "Bl602UartOutStreamBuf.h"

namespace at {

class Bl602UartOutStream : public std::ostream {
public:
    explicit Bl602UartOutStream(uint8_t uartId);

private:
    Bl602UartOutStreamBuf m_buf;
};

}  // namespace at

#endif // defined BL602 || defined BL702
