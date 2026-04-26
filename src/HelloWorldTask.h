#pragma once

#include "Task.h"

namespace at {

class HelloWorldTask : public Task {
protected:
    virtual void routine() override;

public:
    explicit HelloWorldTask();

    virtual ~HelloWorldTask() override;
};

}  // namespace at
