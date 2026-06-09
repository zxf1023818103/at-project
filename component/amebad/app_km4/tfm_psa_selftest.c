/*
 * tfm_psa_selftest.c — app_km4 (NSPE) issues a real PSA client call into TF-M.
 *
 * When AMEBAD_TFM_BOOT is on, app_km4 is TF-M's Non-Secure world. This proves
 * the PSA round-trip works from inside the full driver image (not just the
 * standalone ns_psa_demo): NS tfm_platform_ioctl(PING) -> SG veneer -> SPM ->
 * Platform partition (SFN) -> tfm_platform_hal_ioctl -> reply.
 *
 * Called early from main() before vTaskStartScheduler() — single-threaded, so
 * the bare-metal tfm_ns_interface_dispatch (no mutex) is sufficient here. Once
 * PSA is called concurrently from multiple FreeRTOS tasks, switch the linked
 * dispatch to tfm_ns_interface_rtos.c + a FreeRTOS os_wrapper.
 */
#include <stdint.h>

#include "psa/client.h"
#include "tfm_platform_api.h"

extern int DiagPrintf(const char *fmt, ...);

/* Demo IOCTL contract — must match the secure HAL in
 * component/amebad/app_km4_secure/platform/rtl8721d/services/src/tfm_platform_system.c */
#define AMEBAD_IOCTL_PING        ((tfm_platform_ioctl_req_t)0x8721D0)
#define AMEBAD_IOCTL_PING_XOR    (0x8721D000u)

void amebad_tfm_psa_selftest(void)
{
    uint32_t nonce  = 0x12345678u;
    uint32_t out    = 0u;
    uint32_t expect = nonce ^ AMEBAD_IOCTL_PING_XOR;
    enum tfm_platform_err_t r;

    psa_invec  in = { .base = &nonce, .len = sizeof(nonce) };
    psa_outvec ov = { .base = &out,   .len = sizeof(out)   };

    DiagPrintf("[KM4] PSA: framework_version=0x%x\n\r", psa_framework_version());

    r = tfm_platform_ioctl(AMEBAD_IOCTL_PING, &in, &ov);

    DiagPrintf("[KM4] PSA: ioctl PING ret=%d out=0x%x expect=0x%x -> %s\n\r",
               (int)r, out, expect,
               (r == TFM_PLATFORM_ERR_SUCCESS && out == expect) ? "PASS" : "FAIL");
}
