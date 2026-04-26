#pragma once

#include "Task.h"

namespace at {

class AppTask : public Task {
public:
    explicit AppTask();

    virtual ~AppTask() override;

protected:
    virtual void routine() override;
};

}  // namespace at
