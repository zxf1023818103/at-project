#pragma once
#include "Uart.h"

#include <ostream>

#include "UartOutStreamBuf.h"

namespace at {

class UartOutStream : public std::ostream {
public:
    explicit UartOutStream(UartDevice *dev);

private:
    UartOutStreamBuf m_buf;
};

}  // namespace at
