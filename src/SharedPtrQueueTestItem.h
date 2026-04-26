#pragma once

#include "RefCountObject.h"

namespace at {

struct SharedPtrQueueTestItem : public RefCountObject {
    int value = 0;

    explicit SharedPtrQueueTestItem(int v);

    virtual ~SharedPtrQueueTestItem();
};

}  // namespace at
