#include "efuse_debug.h"
#include <string.h>
/* ACUT ROM entry points from the SDK's secure/nonsecure symbol maps. */
#ifdef DPLUS_EFUSE_SECURE
#define OTP_READ 0xb275u
#define OTP_WRITE 0xb485u
#define OTP_LREAD 0xb769u
#define OTP_LWRITE 0xb891u
#else
#define OTP_READ 0x22c91u
#define OTP_WRITE 0x22ea1u
#define OTP_LREAD 0x23185u
#define OTP_LWRITE 0x232adu
#endif
int dplus_efuse_execute(const struct dplus_efuse_request *r, uint8_t result[32])
{
    if (!r || !result || r->operation > DPLUS_EFUSE_LOGICAL_WRITE ||
        !r->length || r->length > 32) return -1;
    uint32_t limit = (r->operation & 1) ? 0x400 : 0x800;
    if (r->address >= limit || r->length > limit-r->address) return -1;
    int write = r->operation >= DPLUS_EFUSE_RAW_WRITE;
    /* Raw writes must never overwrite the ROM-managed logical-map packets. */
    if (write && (r->confirmation != DPLUS_EFUSE_CONFIRM ||
        (r->operation == DPLUS_EFUSE_RAW_WRITE && r->address < 0x1fd))) return -1;
    int (*read8)(uint32_t,uint8_t *) = (void *)OTP_READ;
    int (*write8)(uint32_t,uint8_t) = (void *)OTP_WRITE;
    int (*lread)(uint8_t *,uint32_t,uint32_t) = (void *)OTP_LREAD;
    int (*lwrite)(uint32_t,uint32_t,uint8_t *) = (void *)OTP_LWRITE;
    memset(result,0,32);
    if (r->operation & 1) {
        uint8_t data[32]; memcpy(data,r->data,r->length);
        if (write && lwrite(r->address,r->length,data) != 1) return -2;
        if (lread(result,r->address,r->length) != 1) return -2;
    } else {
        for (unsigned i=0;i<r->length;++i) {
            if (read8(r->address+i,&result[i]) != 1) return -2;
            if (write && (result[i] & r->data[i]) != r->data[i]) return -1;
        }
        if (write) for (unsigned i=0;i<r->length;++i) {
            if (result[i] != r->data[i] && write8(r->address+i,r->data[i]) != 1) return -2;
            if (read8(r->address+i,&result[i]) != 1) return -2;
        }
    }
    return write && memcmp(result,r->data,r->length) ? -2 : 0;
}
