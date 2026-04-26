#pragma once

#include "Task.h"

namespace at {

class AbstractCallbackTask : public Task {
public:
    explicit AbstractCallbackTask();

    virtual ~AbstractCallbackTask() override;

    virtual bool success() const;
};

}  // namespace at
