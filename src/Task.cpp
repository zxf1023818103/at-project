#include "Task.h"

namespace at {

Task::Task() : RefCountObject() {}

void Task::routine() {
    // my_assert("Should not run here" == nullptr);
}

void Task::taskEntry(void *arg) {
    shared_ptr<Task> *pp = static_cast<shared_ptr<Task> *>(arg);
    (*pp)->routine();
    (*pp)->m_exitedSemaphore.give();
    delete pp;
    ::vTaskDelete(nullptr);
}

bool Task::startSync() {
    if (m_startedSemaphore.give()) {
        routine();
        m_exitedSemaphore.give();
        return true;
    }
    else {
        printf("Sync Task is already started\n");
        return false;
    }
}

bool Task::startAsync(string name, const uint32_t stackSize, const uint8_t priority) const {
    (void) stackSize;
    (void) priority;

    my_assert(name.empty() == false);
    my_assert(stackSize >= configMINIMAL_STACK_SIZE);
    my_assert(priority < configMAX_PRIORITIES);
    if (m_startedSemaphore.give()) {
        auto p = new shared_ptr(shared_from_this());
        my_assert(::xTaskCreate(taskEntry, name.c_str(), stackSize, p, priority, nullptr));
        return true;
    }
    else {
        printf("Async Task is already started\n");
        return false;
    }
}

void Task::waitForStart() const {
    m_startedSemaphore.take();
    m_startedSemaphore.give();
}

bool Task::waitForStart(uint32_t timeoutMsec) const {
    if (m_startedSemaphore.take(timeoutMsec)) {
        m_startedSemaphore.give();
        return true;
    }
    return false;
}

void Task::waitForExit() const {
    m_exitedSemaphore.take();
    m_exitedSemaphore.give();
}

bool Task::waitForExit(uint32_t timeoutMsec) const {
    if (m_exitedSemaphore.take(timeoutMsec)) {
        m_exitedSemaphore.give();
        return true;
    }
    return false;
}

Task::~Task() {
    if (!m_startedSemaphore.take(0)) {
        // Task was never started
        return;
    }
    m_exitedSemaphore.take();
}

}  // namespace at
