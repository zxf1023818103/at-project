#include "CallbackTaskSharedPtrIoQueue.h"

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

namespace at {

CallbackTaskSharedPtrIoQueue::CallbackTaskSharedPtrIoQueue(size_t length)
    : BluetoothTaskIoQueueHolder<shared_ptr<AbstractCallbackTask>>(length) {}

CallbackTaskSharedPtrIoQueue::~CallbackTaskSharedPtrIoQueue() = default;

bool CallbackTaskSharedPtrIoQueue::sendInternal(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutTicks, ::T_IO_MSG_TYPE type) const {
    shared_ptr<AbstractCallbackTask> *taskPtrPtr = new shared_ptr<AbstractCallbackTask>(taskPtr);
    bool ret = Parent::sendInternal(taskPtrPtr, timeoutTicks, type);
    if (!ret) {
        delete taskPtrPtr;
    }
    return ret;
}

bool CallbackTaskSharedPtrIoQueue::send(shared_ptr<AbstractCallbackTask> taskPtr, ::T_IO_MSG_TYPE type) const {
    return sendInternal(taskPtr, portMAX_DELAY, type);
}

bool CallbackTaskSharedPtrIoQueue::send(shared_ptr<AbstractCallbackTask> taskPtr, uint32_t timeoutMsec, ::T_IO_MSG_TYPE type) const {
    return sendInternal(taskPtr, pdMS_TO_TICKS(timeoutMsec), type);
}

}  // namespace at

#endif // realtek platforms
