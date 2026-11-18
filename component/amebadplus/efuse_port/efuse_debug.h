/* Debug-only provisioning ABI. Never included in a production secure service. */
#ifndef DPLUS_EFUSE_DEBUG_H
#define DPLUS_EFUSE_DEBUG_H
#include <stdint.h>
#define DPLUS_EFUSE_IOCTL 0x8721e0
#define DPLUS_EFUSE_CONFIRM 0x434f4e46u
enum { DPLUS_EFUSE_RAW_READ, DPLUS_EFUSE_LOGICAL_READ,
       DPLUS_EFUSE_RAW_WRITE, DPLUS_EFUSE_LOGICAL_WRITE };
struct dplus_efuse_request {
    uint32_t operation, address, length, confirmation;
    uint8_t data[32];
};
int dplus_efuse_execute(const struct dplus_efuse_request *, uint8_t result[32]);
#endif
