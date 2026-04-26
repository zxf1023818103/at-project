#pragma once

#include "RefCountObject.h"

namespace at {

template<typename T>
class RtosQueueHolder : public RefCountObject {
private:
    const ::QueueHandle_t m_handle;

    const size_t m_length;

protected:
    bool receiveInternal(T* item, uint32_t timeoutTicks) const {
        (void) item;
        (void) timeoutTicks;
        return ::xQueueReceive(m_handle, item, timeoutTicks) == pdTRUE;
    }

    bool sendInternal(T* item, uint32_t timeoutTicks) const {
        (void) item;
        (void) timeoutTicks;
        return ::xQueueSend(m_handle, item, timeoutTicks) == pdTRUE;
    }

public:
    explicit RtosQueueHolder(size_t length) : RefCountObject(), m_handle(::xQueueCreate(length, sizeof(T))), m_length(length) {
        my_assert(m_handle);
    }

    virtual ~RtosQueueHolder() {
        ::vQueueDelete(m_handle);
    }

    ::QueueHandle_t handle() const {
        return m_handle;
    }

    size_t length() const {
        return m_length;
    }

    void receive(T *item) const {
        (void) item;
        my_assert(receiveInternal(item, portMAX_DELAY));
    }

    bool receive(T *item, uint32_t timeoutMsec) const {
        (void) item;
        (void) timeoutMsec;
        return receiveInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }

    void send(T *item) const {
        (void) item;
        my_assert(sendInternal(item, portMAX_DELAY));
    }

    bool send(T *item, uint32_t timeoutMsec) const {
        (void) item;
        (void) timeoutMsec;
        return sendInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }
};

}  // namespace at
