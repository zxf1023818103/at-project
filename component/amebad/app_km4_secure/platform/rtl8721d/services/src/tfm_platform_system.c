/*
 * tfm_platform_system.c — RTL8721D (Ameba-D) implementation of the TF-M
 * Platform PSA-RoT service HAL.
 *
 * The Platform partition (secure_fw/partitions/platform) routes the PSA service
 * requests TFM_PLATFORM_API_ID_{SYSTEM_RESET,IOCTL} to these HAL entry points.
 * The NV-counter sub-module is disabled for this port
 * (PLATFORM_NV_COUNTER_MODULE_DISABLED=1, config.cmake), so only system_reset
 * and ioctl are implemented here.
 *
 * IOCTL is the PSA bring-up demo round-trip: the Non-Secure side issues
 * tfm_platform_ioctl(AMEBAD_IOCTL_PING, {nonce}, {out}); this code runs in the
 * secure partition, transforms the nonce, and writes the result back. The NS
 * side verifying out == (nonce ^ AMEBAD_IOCTL_PING_XOR) proves the full path
 * NS psa_call -> SG veneer -> SPM -> partition SFN -> HAL -> reply works.
 */
#include <stdint.h>
#include <string.h>

#include "tfm_platform_system.h"
#include "tfm_hal_device_header.h"   /* CMSIS core: NVIC_SystemReset() */

/* Demo IOCTL contract, shared with the NS client (app_km4 side keeps a matching
 * copy). PING takes one u32 input vector and returns one u32 output vector. */
#define AMEBAD_IOCTL_PING        ((tfm_platform_ioctl_req_t)0x8721D0)
#define AMEBAD_IOCTL_PING_XOR    (0x8721D000u)

void tfm_platform_hal_system_reset(void)
{
    /* Flush any platform state here if needed, then reset the SoC. The KM4 is
     * the core that owns the secure world; a core-level reset is sufficient for
     * the service contract (the Ameba boot chain re-runs from ROM). */
    NVIC_SystemReset();
}

enum tfm_platform_err_t tfm_platform_hal_ioctl(tfm_platform_ioctl_req_t request,
                                               psa_invec  *in_vec,
                                               psa_outvec *out_vec)
{
    switch (request) {
    case AMEBAD_IOCTL_PING: {
        uint32_t nonce;
        uint32_t result;

        if (in_vec == NULL || in_vec->base == NULL ||
            in_vec->len != sizeof(uint32_t)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        if (out_vec == NULL || out_vec->base == NULL ||
            out_vec->len < sizeof(uint32_t)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }

        memcpy(&nonce, in_vec->base, sizeof(nonce));
        result = nonce ^ AMEBAD_IOCTL_PING_XOR;     /* computed in secure world */
        memcpy(out_vec->base, &result, sizeof(result));
        out_vec->len = sizeof(result);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    default:
        return TFM_PLATFORM_ERR_NOT_SUPPORTED;
    }
}
