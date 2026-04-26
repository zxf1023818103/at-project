#include "AbstractCallbackTask.h"

namespace at {

AbstractCallbackTask::AbstractCallbackTask() : Task() {}

AbstractCallbackTask::~AbstractCallbackTask() = default;

bool AbstractCallbackTask::success() const {
    return false;
}

}  // namespace at
