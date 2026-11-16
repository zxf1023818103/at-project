/* Debug-only provisioning ABI. Never included in a production secure service. */
#ifndef GREEN2_EFUSE_DEBUG_H
#define GREEN2_EFUSE_DEBUG_H
#include <stdint.h>
#define GREEN2_EFUSE_IOCTL 0x8721e0
#define GREEN2_EFUSE_CONFIRM 0x434f4e46u
enum { GREEN2_EFUSE_RAW_READ, GREEN2_EFUSE_LOGICAL_READ,
       GREEN2_EFUSE_RAW_WRITE, GREEN2_EFUSE_LOGICAL_WRITE };
struct green2_efuse_request {
    uint32_t operation, address, length, confirmation;
    uint8_t data[32];
};
int green2_efuse_execute(const struct green2_efuse_request *, uint8_t result[32]);
#endif
