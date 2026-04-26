#pragma once

#include "Semaphore.h"

namespace at {

class Task : public RefCountObject {
private:
    const Semaphore m_startedSemaphore;

    const Semaphore m_exitedSemaphore;

    static void taskEntry(void *arg);

protected:
    virtual void routine();

public:
    explicit Task();

    bool startSync();

    bool startAsync(string name, const uint32_t stackSize = 1024, const uint8_t priority = tskIDLE_PRIORITY) const;

    virtual void waitForStart() const;

    bool waitForStart(uint32_t timeoutMsec) const;

    void waitForExit() const;

    bool waitForExit(uint32_t timeoutMsec) const;

    virtual ~Task();
};

}  // namespace at
