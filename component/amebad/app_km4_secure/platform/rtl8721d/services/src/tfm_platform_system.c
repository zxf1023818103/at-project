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
#include "efuse_huk.h"               /* amebad_secure_read_efuse + AMEBAD_EFUSE_HUK_* */

/* Demo IOCTL contract, shared with the NS client (app_km4 side keeps a matching
 * copy). PING takes one u32 input vector and returns one u32 output vector. */
#define AMEBAD_IOCTL_PING        ((tfm_platform_ioctl_req_t)0x8721D0)
#define AMEBAD_IOCTL_PING_XOR    (0x8721D000u)

/* eFuse HUK info: read the 32-byte TrustZone-secure user eFuse (the would-be HUK)
 * and return a NON-SECRET summary {blank, fingerprint}. Used to validate that the
 * secure world can read the secure eFuse and to tell blank-vs-programmed, WITHOUT
 * leaking the key (only a folded 32-bit hash + a blank flag cross the boundary). */
#define AMEBAD_IOCTL_EFUSE_HUK_INFO  ((tfm_platform_ioctl_req_t)0x8721E0)

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
    case AMEBAD_IOCTL_EFUSE_HUK_INFO: {
        uint8_t  huk[AMEBAD_EFUSE_HUK_SIZE];
        uint32_t out[2];      /* {blank, fingerprint} — non-secret */
        uint32_t fp = 0x811C9DC5u;   /* FNV-1a */
        uint32_t i;
        int all_ff = 1, all_00 = 1;

        if (out_vec == NULL || out_vec->base == NULL || out_vec->len < sizeof(out)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }

        amebad_secure_read_efuse(AMEBAD_EFUSE_HUK_ADDR, AMEBAD_EFUSE_HUK_SIZE, huk);
        for (i = 0; i < AMEBAD_EFUSE_HUK_SIZE; i++) {
            fp = (fp ^ huk[i]) * 0x01000193u;
            if (huk[i] != 0xFFu) { all_ff = 0; }
            if (huk[i] != 0x00u) { all_00 = 0; }
        }
        out[0] = (uint32_t)((all_ff || all_00) ? 1 : 0);   /* blank? */
        out[1] = fp;                                        /* fingerprint */
        memset(huk, 0, sizeof(huk));                        /* wipe the key from the stack */

        memcpy(out_vec->base, out, sizeof(out));
        out_vec->len = sizeof(out);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    default:
        return TFM_PLATFORM_ERR_NOT_SUPPORTED;
    }
}
