#include "Uart.h"

#include "UartDispatch.h"
#include "UartInStreamBuf.h"
#include "UartOutStreamBuf.h"

#if defined AT_UART_BACKEND_BFLB
#  include <bflb_uart.h>
#  include <bflb_irq.h>
#elif defined AT_UART_BACKEND_AMEBA
#  include "UartAmebaGlue.h"
#endif

namespace at {

UartDispatch::Slot UartDispatch::s_slots[UartDispatch::kMaxSlots] = {};

namespace {

#if defined AT_UART_BACKEND_BFLB
void uart_isr_bflb(int /*irq*/, void *arg) {
    auto *slot = static_cast<UartDispatch::Slot *>(arg);
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
#elif defined AT_UART_BACKEND_AMEBA
void uart_isr_ameba(uint32_t id, at_uart_irq_t event) {
    auto *slot = reinterpret_cast<UartDispatch::Slot *>(static_cast<uintptr_t>(id));
    BaseType_t woken = pdFALSE;

    if (event == AT_UART_IRQ_RX && slot->in) {
        slot->in->onRxIsr(woken);
    } else if (event == AT_UART_IRQ_TX && slot->out) {
        slot->out->onTxIsr(woken);
    }

    portYIELD_FROM_ISR(woken);
}
#endif

}  // namespace

UartDispatch::Slot *UartDispatch::findOrCreate(UartDevice *dev) {
    Slot *empty = nullptr;
    for (auto &s : s_slots) {
        if (s.dev == dev) return &s;
        if (!s.dev && !empty) empty = &s;
    }
    if (empty) {
        empty->dev = dev;
        empty->in  = nullptr;
        empty->out = nullptr;
#if defined AT_UART_BACKEND_BFLB
        bflb_irq_attach(dev->irq_num, &uart_isr_bflb, empty);
        bflb_irq_enable(dev->irq_num);
#elif defined AT_UART_BACKEND_AMEBA
        at_uart_irq_handler_install(dev, &uart_isr_ameba,
                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(empty)));
#endif
    }
    return empty;
}

void UartDispatch::releaseIfEmpty(Slot *slot) {
    if (!slot->in && !slot->out) {
#if defined AT_UART_BACKEND_BFLB
        bflb_irq_disable(slot->dev->irq_num);
#elif defined AT_UART_BACKEND_AMEBA
        at_uart_irq_set(slot->dev, AT_UART_IRQ_RX, 0);
        at_uart_irq_set(slot->dev, AT_UART_IRQ_TX, 0);
#endif
        slot->dev = nullptr;
    }
}

void UartDispatch::registerIn(UartDevice *dev, UartInStreamBuf *buf) {
    Slot *slot = findOrCreate(dev);
    if (slot) slot->in = buf;
}

void UartDispatch::unregisterIn(UartDevice *dev) {
    for (auto &s : s_slots) {
        if (s.dev == dev) {
            s.in = nullptr;
            releaseIfEmpty(&s);
            break;
        }
    }
}

void UartDispatch::registerOut(UartDevice *dev, UartOutStreamBuf *buf) {
    Slot *slot = findOrCreate(dev);
    if (slot) slot->out = buf;
}

void UartDispatch::unregisterOut(UartDevice *dev) {
    for (auto &s : s_slots) {
        if (s.dev == dev) {
            s.out = nullptr;
            releaseIfEmpty(&s);
            break;
        }
    }
}

}  // namespace at
