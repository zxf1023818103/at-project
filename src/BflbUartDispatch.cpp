#if defined BL602 || defined BL702

#include "BflbUartDispatch.h"
#include "BflbUartInStreamBuf.h"
#include "BflbUartOutStreamBuf.h"

#include <bflb_uart.h>
#include <bflb_irq.h>

namespace at {

BflbUartDispatch::Slot BflbUartDispatch::s_slots[BflbUartDispatch::kMaxSlots] = {};

BflbUartDispatch::Slot *BflbUartDispatch::findOrCreate(struct bflb_device_s *dev) {
    Slot *empty = nullptr;
    for (auto &s : s_slots) {
        if (s.dev == dev) return &s;
        if (!s.dev && !empty) empty = &s;
    }
    if (empty) {
        empty->dev = dev;
        empty->in  = nullptr;
        empty->out = nullptr;
        bflb_irq_attach(dev->irq_num, &BflbUartDispatch::isr, empty);
        bflb_irq_enable(dev->irq_num);
    }
    return empty;
}

void BflbUartDispatch::releaseIfEmpty(Slot *slot) {
    if (!slot->in && !slot->out) {
        bflb_irq_disable(slot->dev->irq_num);
        slot->dev = nullptr;
    }
}

void BflbUartDispatch::registerIn(struct bflb_device_s *dev, BflbUartInStreamBuf *buf) {
    Slot *slot = findOrCreate(dev);
    if (slot) slot->in = buf;
}

void BflbUartDispatch::unregisterIn(struct bflb_device_s *dev) {
    for (auto &s : s_slots) {
        if (s.dev == dev) {
            s.in = nullptr;
            releaseIfEmpty(&s);
            break;
        }
    }
}

void BflbUartDispatch::registerOut(struct bflb_device_s *dev, BflbUartOutStreamBuf *buf) {
    Slot *slot = findOrCreate(dev);
    if (slot) slot->out = buf;
}

void BflbUartDispatch::unregisterOut(struct bflb_device_s *dev) {
    for (auto &s : s_slots) {
        if (s.dev == dev) {
            s.out = nullptr;
            releaseIfEmpty(&s);
            break;
        }
    }
}

void BflbUartDispatch::isr(int /*irq*/, void *arg) {
    auto *slot = static_cast<Slot *>(arg);
    BaseType_t woken = pdFALSE;
    uint32_t pending = bflb_uart_get_intstatus(slot->dev);

    if (slot->in && (pending & (UART_INTSTS_RX_FIFO | UART_INTSTS_RTO))) {
        slot->in->onRxIsr(woken);
    }
    if (slot->out && (pending & UART_INTSTS_TX_FIFO)) {
        slot->out->onTxIsr(woken);
    }

    bflb_uart_int_clear(slot->dev, pending);
    portYIELD_FROM_ISR(woken);
}

}  // namespace at

#endif // defined BL602 || defined BL702
