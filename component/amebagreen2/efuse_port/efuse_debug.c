#include "efuse_debug.h"
#include <string.h>
/* Resolve the chip's ROM ABI through its versioned linker map. */
extern int OTP_Read8(uint32_t, uint8_t *);
extern int OTP_Write8(uint32_t, uint8_t);
extern int OTP_LogicalRead(uint8_t *, uint32_t, uint32_t);
extern int OTP_LogicalWrite(uint32_t, uint32_t, uint8_t *);
int green2_efuse_execute(const struct green2_efuse_request *r, uint8_t result[32])
{
    if (!r || !result || r->operation > GREEN2_EFUSE_LOGICAL_WRITE ||
        !r->length || r->length > 32) return -1;
    uint32_t limit = (r->operation & 1) ? 0x400 : 0x800;
    if (r->address >= limit || r->length > limit-r->address) return -1;
    int write = r->operation >= GREEN2_EFUSE_RAW_WRITE;
    /* Raw writes must never overwrite the ROM-managed logical-map packets. */
    if (write && (r->confirmation != GREEN2_EFUSE_CONFIRM ||
        (r->operation == GREEN2_EFUSE_RAW_WRITE && r->address < 0x1fd))) return -1;
    memset(result,0,32);
    if (r->operation & 1) {
        uint8_t data[32]; memcpy(data,r->data,r->length);
        if (write && OTP_LogicalWrite(r->address,r->length,data) != 1) return -2;
        if (OTP_LogicalRead(result,r->address,r->length) != 1) return -2;
    } else {
        for (unsigned i=0;i<r->length;++i) {
            if (OTP_Read8(r->address+i,&result[i]) != 1) return -2;
            if (write && (result[i] & r->data[i]) != r->data[i]) return -1;
        }
        if (write) for (unsigned i=0;i<r->length;++i) {
            if (result[i] != r->data[i] && OTP_Write8(r->address+i,r->data[i]) != 1) return -2;
            if (OTP_Read8(r->address+i,&result[i]) != 1) return -2;
        }
    }
    return write && memcmp(result,r->data,r->length) ? -2 : 0;
}
