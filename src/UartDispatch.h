#pragma once
#include "Uart.h"

#include <cstddef>

namespace at {

class UartInStreamBuf;
class UartOutStreamBuf;

// One IRQ vector per UART; registering both an InStreamBuf and an OutStreamBuf
// directly with the SDK would have the second overwrite the first. This
// dispatcher owns the ISR per device and routes RX/TX work to the registered
// half-duplex streambufs.
class UartDispatch {
public:
    // Public so the platform-specific ISR trampolines in UartDispatch.cpp
    // (anonymous-namespace functions) can fan RX/TX out to the registered
    // streambufs.
    struct Slot {
        UartDevice       *dev;
        UartInStreamBuf  *in;
        UartOutStreamBuf *out;
    };

    static void registerIn(UartDevice *dev, UartInStreamBuf *buf);
    static void unregisterIn(UartDevice *dev);
    static void registerOut(UartDevice *dev, UartOutStreamBuf *buf);
    static void unregisterOut(UartDevice *dev);

private:
    static constexpr size_t kMaxSlots = 4;
    static Slot s_slots[kMaxSlots];

    static Slot *findOrCreate(UartDevice *dev);
    static void  releaseIfEmpty(Slot *slot);
};

}  // namespace at
