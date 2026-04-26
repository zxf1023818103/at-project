#include "HelloWorldTask.h"

namespace at {

HelloWorldTask::HelloWorldTask() : Task() {}

HelloWorldTask::~HelloWorldTask() = default;

void HelloWorldTask::routine() {
    for (int i = 0; i < 5; ++i) {
        printf("HelloWorldTask started: %d\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace at
