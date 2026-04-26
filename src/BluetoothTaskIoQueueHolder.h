#pragma once

#include "Common.h"

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#include "RtosQueueHolder.h"

namespace at {

using BluetoothTaskEventQueueHolder = RtosQueueHolder<uint8_t>;

template<typename T>
class BluetoothTaskIoQueueHolder : public RtosQueueHolder<::T_IO_MSG> {
public:
    using Parent = RtosQueueHolder<::T_IO_MSG>;

    BluetoothTaskIoQueueHolder(size_t length) : RtosQueueHolder<::T_IO_MSG>(length) {}

    virtual ~BluetoothTaskIoQueueHolder() override {
        for (;;) {
            ::T_IO_MSG msg;
            if (Parent::receive(&msg, 0)) {
                if (msg.type == ::IO_MSG_TYPE_QDECODE && msg.u.buf) {
                    auto *pp = static_cast<T*>(msg.u.buf);
                    delete pp;
                }
            } else {
                break;
            }
        }
    }

protected:
    bool sendInternal(T* item, uint32_t timeoutTicks, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const {
        (void) item;
        (void) timeoutTicks;

        ::T_IO_MSG msg = {
            .type = type,
            .subtype = 0,
            .u = { .buf = static_cast<void*>(item) }
        };
        return Parent::sendInternal(&msg, timeoutTicks);
    }
};

}  // namespace at

#endif // realtek platforms
