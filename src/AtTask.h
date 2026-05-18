#pragma once

#include "Task.h"

namespace at {

class AtTask : public Task {
public:
    explicit AtTask();

    virtual ~AtTask() override;

protected:
    virtual void routine() override;
};

}  // namespace at
