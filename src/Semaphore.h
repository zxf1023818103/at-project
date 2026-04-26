#pragma once

#include "RefCountObject.h"

namespace at {

class Semaphore : public RefCountObject {
private:
    const ::SemaphoreHandle_t m_handle;

    bool takeInternal(uint32_t timeoutTicks) const;

public:
    explicit Semaphore();

    virtual ~Semaphore();

    bool give() const;

    void take() const;

    bool take(uint32_t timeoutMsec) const;
};

}  // namespace at
