#pragma once

#include "Common.h"

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#include "BluetoothTaskIoQueueHolder.h"
#include "AbstractCallbackTask.h"

namespace at {

class CallbackTaskSharedPtrIoQueue: public BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>> {
public:
    using Parent = BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>>;

protected:
    bool sendInternal(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutTicks, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const;

public:
    explicit CallbackTaskSharedPtrIoQueue(size_t length);

    virtual ~CallbackTaskSharedPtrIoQueue() override;

    bool send(shared_ptr<AbstractCallbackTask> taskPtr, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const;

    bool send(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutMsec, ::T_IO_MSG_TYPE type = ::IO_MSG_TYPE_QDECODE) const;
};

}  // namespace at

#endif // realtek platforms
