#include <stdint.h>
#include <string.h>
#include "tfm_platform_system.h"
#include "tfm_hal_device_header.h"
#include "region_defs.h"
#ifdef GREEN2_EFUSE_DEBUG
#include "efuse_debug.h"
#endif
extern int green2_secure_flash_modify(uint32_t, const void *, uint32_t, int);
extern int green2_secure_flash_status(int32_t, uint8_t [4]);
static enum tfm_platform_err_t flash_gateway(uint32_t request, psa_invec *in)
{
    if (!in || in->len < 8) return TFM_PLATFORM_ERR_INVALID_PARAM;
    uint32_t args[2]; memcpy(args, in->base, sizeof(args));
    uint32_t addr = args[0], len = args[1];
    int erase = request == 0x8721d2;
    const uint8_t *bytes = (const uint8_t *)in->base + 8;
    if (!len || addr >= 0x3f0000 || len > 0x3f0000 - addr ||
        (erase ? (len != 4096 || (addr & 4095) || in->len != 8) :
                 (len > 256 || in->len != 8 + len))) return TFM_PLATFORM_ERR_INVALID_PARAM;
    uint32_t active = ((volatile uint32_t *)NS_HANDOFF_BLOCK)[3];
    if (active != 0x10000 && active != 0x190000) return TFM_PLATFORM_ERR_SYSTEM_ERROR;
    uint32_t inactive = active == 0x10000 ? 0x190000 : 0x10000;
    int allowed = addr >= 0x300000 ||
        (addr >= inactive && addr < inactive + 0x170000 && len <= inactive + 0x170000 - addr);
    /* OTA commit may invalidate only the old certificate, after the destination
     * has a committed certificate. The running SPE/code remains immutable. */
    if (!allowed && !erase && addr == active && len == 8 &&
        !memcmp((const void *)(0x08000000 + inactive), "81958711", 8)) {
        unsigned value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= bytes[i];
        allowed = value == 0;
    }
    if (!allowed) return TFM_PLATFORM_ERR_NOT_SUPPORTED;
    return green2_secure_flash_modify(addr, bytes, len, erase) ?
        TFM_PLATFORM_ERR_SYSTEM_ERROR : TFM_PLATFORM_ERR_SUCCESS;
}
void tfm_platform_hal_system_reset(void) { NVIC_SystemReset(); }
enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                               psa_invec *in, psa_outvec *out)
{
    if (request == 0x8721d6) {
        if (!in || in->len!=8) return TFM_PLATFORM_ERR_INVALID_PARAM;
        uint32_t args[2]; memcpy(args,in->base,sizeof(args));
        uint32_t active=((volatile uint32_t *)NS_HANDOFF_BLOCK)[3];
        if ((active!=0x10000 && active!=0x190000) ||
            args[0]!=(active==0x10000 ? 0x190000u : 0x10000u))
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        extern int green2_verify_flash_layout(uint32_t,uint32_t,int);
        return green2_verify_flash_layout(args[0],args[1],1) ?
            TFM_PLATFORM_ERR_SYSTEM_ERROR : TFM_PLATFORM_ERR_SUCCESS;
    }
#ifdef GREEN2_TFM_STORAGE
    /* Bounded health regression probe; never exposes raw entropy to NS. */
    if (request == 0x8721d5) {
        if (!in || in->len != 4) return TFM_PLATFORM_ERR_INVALID_PARAM;
        uint32_t count; memcpy(&count,in->base,4);
        if (!count || count > 4096) return TFM_PLATFORM_ERR_INVALID_PARAM;
        extern int green2_secure_random(unsigned char *, size_t);
        unsigned char discard[64];
        int failed = 0;
        while (count && !failed) {
            unsigned n = count > sizeof(discard) ? sizeof(discard) : count;
            failed = green2_secure_random(discard,n);
            count -= n;
        }
        for (unsigned i=0;i<sizeof(discard);++i) ((volatile unsigned char *)discard)[i]=0;
        return failed ? TFM_PLATFORM_ERR_SYSTEM_ERROR : TFM_PLATFORM_ERR_SUCCESS;
    }
#endif
    if (request == 0x8721d4) {
        if (!in || !out || in->len != 4 || out->len < 4) return TFM_PLATFORM_ERR_INVALID_PARAM;
        int32_t mode; uint8_t status[4]; memcpy(&mode,in->base,4);
        if (mode < -1 || mode > 2) return TFM_PLATFORM_ERR_INVALID_PARAM;
        if (green2_secure_flash_status(mode,status)) return TFM_PLATFORM_ERR_SYSTEM_ERROR;
        memcpy(out->base,status,4); out->len=4; return TFM_PLATFORM_ERR_SUCCESS;
    }
#ifdef GREEN2_EFUSE_DEBUG
    if (request == GREEN2_EFUSE_IOCTL) {
        if (!in || !out || in->len != sizeof(struct green2_efuse_request) || out->len < 32)
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        struct green2_efuse_request r;
        uint8_t result[32]; memcpy(&r,in->base,sizeof(r));
        if (green2_efuse_execute(&r,result)) return TFM_PLATFORM_ERR_SYSTEM_ERROR;
        memcpy(out->base,result,32); out->len=32; return TFM_PLATFORM_ERR_SUCCESS;
    }
#endif
    if (request == 0x8721d3 && out && out->len >= 4) {
        uint32_t active = ((volatile uint32_t *)NS_HANDOFF_BLOCK)[3];
        memcpy(out->base, &active, 4); out->len = 4;
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    if (request == 0x8721d1 || request == 0x8721d2) return flash_gateway(request, in);
    if (request != 0x8721D0 || !in || !out || in->len != 4 || out->len < 4)
        return TFM_PLATFORM_ERR_INVALID_PARAM;
    uint32_t value;
    memcpy(&value, in->base, 4);
    value ^= 0x5AA55AA5;
    memcpy(out->base, &value, 4); out->len = 4;
    return TFM_PLATFORM_ERR_SUCCESS;
}
