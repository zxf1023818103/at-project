#pragma once

#include "Common.h"

namespace at {

struct MacAddress {
    array<uint8_t, 6> address;

    string toString() const;

    string toHex() const;
};

}  // namespace at
