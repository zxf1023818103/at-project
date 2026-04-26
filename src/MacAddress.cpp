#include "MacAddress.h"

namespace at {

string MacAddress::toString() const {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             address[0], address[1], address[2],
             address[3], address[4], address[5]);
    return string(buf);
}

string MacAddress::toHex() const {
    char buf[13];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
             address[0], address[1], address[2],
             address[3], address[4], address[5]);
    return string(buf);
}

}  // namespace at
