#include "Semaphore.h"

namespace at {

Semaphore::Semaphore() : RefCountObject(), m_handle(::xSemaphoreCreateBinary()) {
    my_assert(m_handle);
}

Semaphore::~Semaphore() {
    ::vSemaphoreDelete(m_handle);
}

bool Semaphore::takeInternal(uint32_t timeoutTicks) const {
    return ::xSemaphoreTake(m_handle, timeoutTicks) == pdTRUE;
}

bool Semaphore::give() const {
    return ::xSemaphoreGive(m_handle) == pdTRUE;
}

void Semaphore::take() const {
    my_assert(takeInternal(portMAX_DELAY));
}

bool Semaphore::take(uint32_t timeoutMsec) const {
    return takeInternal(pdMS_TO_TICKS(timeoutMsec));
}

}  // namespace at
