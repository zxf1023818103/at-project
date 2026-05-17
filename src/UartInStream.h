#pragma once
#include "Uart.h"

#include <istream>

#include "UartInStreamBuf.h"

namespace at {

class UartInStream : public std::istream {
public:
    explicit UartInStream(UartDevice *dev);

private:
    UartInStreamBuf m_buf;
};

}  // namespace at
