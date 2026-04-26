#pragma once

#include "Common.h"

namespace at {

class RefCountObject : public enable_shared_from_this<RefCountObject> {
public:
    explicit RefCountObject();

    RefCountObject(const RefCountObject&) = delete;

    RefCountObject(const RefCountObject&&) = delete;

    RefCountObject& operator=(const RefCountObject&) = delete;

    RefCountObject& operator=(RefCountObject&&) = delete;

    virtual ~RefCountObject();
};

}  // namespace at
