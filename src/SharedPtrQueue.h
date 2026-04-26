#pragma once

#include "RtosQueueHolder.h"

namespace at {

template<typename T>
class SharedPtrQueue : public RtosQueueHolder<shared_ptr<T> *> {
public:
    using Parent = RtosQueueHolder<shared_ptr<T> *>;

private:
    shared_ptr<T> receiveInternal(uint32_t timeoutTicks) const {
        shared_ptr<T> *pp = nullptr;
        if (Parent::receiveInternal(&pp, timeoutTicks)) {
            my_assert(pp != nullptr);
            shared_ptr<T> p = *pp;
            delete pp;
            return p;
        }
        return nullptr;
    }

    bool sendInternal(const shared_ptr<T> &item, uint32_t timeoutTicks) const {
        shared_ptr<T> *p = new shared_ptr<T>(item);
        bool ret = Parent::sendInternal(&p, timeoutTicks);
        if (!ret) {
            delete p;
        }
        return ret;
    }

public:
    explicit SharedPtrQueue(size_t length) : RtosQueueHolder<shared_ptr<T> *>(length) {
    }

    bool send(const shared_ptr<T> &item) const {
        return sendInternal(item, portMAX_DELAY);
    }

    bool send(const shared_ptr<T> &item, uint32_t timeoutMsec) const {
        return sendInternal(item, pdMS_TO_TICKS(timeoutMsec));
    }

    shared_ptr<T> receive() const {
        return receiveInternal(portMAX_DELAY);
    }

    shared_ptr<T> receive(uint32_t timeoutMsec) const {
        return receiveInternal(pdMS_TO_TICKS(timeoutMsec));
    }

    virtual ~SharedPtrQueue() override {
        while (receive(0) != nullptr) {}
    }
};

}  // namespace at
