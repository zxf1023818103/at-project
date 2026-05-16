#pragma once
#if defined BL602 || defined BL702

#include <cstddef>

struct bflb_device_s;

namespace at {

class BflbUartInStreamBuf;
class BflbUartOutStreamBuf;

// One IRQ vector per UART; registering both an InStreamBuf and an OutStreamBuf
// directly via bflb_irq_attach would have the second overwrite the first. This
// dispatcher owns the ISR per device and routes RX/TX work to the registered
// half-duplex streambufs.
class BflbUartDispatch {
public:
    static void registerIn(struct bflb_device_s *dev, BflbUartInStreamBuf *buf);
    static void unregisterIn(struct bflb_device_s *dev);
    static void registerOut(struct bflb_device_s *dev, BflbUartOutStreamBuf *buf);
    static void unregisterOut(struct bflb_device_s *dev);

private:
    struct Slot {
        struct bflb_device_s *dev;
        BflbUartInStreamBuf  *in;
        BflbUartOutStreamBuf *out;
    };

    // Per-UART slots; BL602 has UART0/UART1. Two is enough but reserve four
    // headroom for future chips that expose more UARTs.
    static constexpr size_t kMaxSlots = 4;
    static Slot s_slots[kMaxSlots];

    static Slot *findOrCreate(struct bflb_device_s *dev);
    static void  releaseIfEmpty(Slot *slot);
    static void  isr(int irq, void *arg);
};

}  // namespace at

#endif // defined BL602 || defined BL702
