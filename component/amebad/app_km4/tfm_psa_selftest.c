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
#define AMEBAD_IOCTL_EFUSE_HUK_INFO  ((tfm_platform_ioctl_req_t)0x8721E0)
#define AMEBAD_IOCTL_HWCRYPTO_KAT    ((tfm_platform_ioctl_req_t)0x8721F0)
#define AMEBAD_IOCTL_HWCRYPTO_GCM    ((tfm_platform_ioctl_req_t)0x872200)

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

    /* eFuse HUK probe: read the 32-byte TrustZone-secure user eFuse in the secure
     * world, return only a non-secret summary {blank, fingerprint}. Proves the SPE
     * can read the secure eFuse + tells blank-vs-programmed (the would-be HUK). */
    {
        uint32_t info[2] = { 0, 0 };
        psa_outvec ov2 = { .base = info, .len = sizeof(info) };
        enum tfm_platform_err_t r2 =
            tfm_platform_ioctl(AMEBAD_IOCTL_EFUSE_HUK_INFO, NULL, &ov2);
        DiagPrintf("[KM4] eFuse HUK(0x150,32B): ret=%d blank=%d fingerprint=0x%08x\n\r",
                   (int)r2, (int)info[0], info[1]);
    }

    /* HW-crypto KAT from the SECURE side: the IPSEC AES engine is secure-only
     * attributed (NS SecureFaults on it). Drive the AES-128-ECB KAT one step at a
     * time so we see exactly where it dies if the engine hangs/faults in the SPE.
     * step 0=CRYPTO_Init 1=aes_ecb_init 2=aes_ecb_encrypt(prior death) 3=compare. */
    {
        const char *name[4] = { "CRYPTO_Init", "aes_ecb_init", "aes_ecb_encrypt", "compare" };
        uint32_t s;
        DiagPrintf("[HWKAT] secure-side IPSEC AES-128-ECB KAT (FIPS-197)...\n\r");
        for (s = 0; s < 4; s++) {
            uint32_t in = s, res = 0;
            psa_invec  iv = { .base = &in,  .len = sizeof(in)  };
            psa_outvec ov = { .base = &res, .len = sizeof(res) };
            enum tfm_platform_err_t rk =
                tfm_platform_ioctl(AMEBAD_IOCTL_HWCRYPTO_KAT, &iv, &ov);
            if (s < 3) {
                DiagPrintf("[HWKAT] step %d %s: ioctl=%d ret=%d\n\r",
                           (int)s, name[s], (int)rk, (int)res);
            } else {
                DiagPrintf("[HWKAT] step %d %s: ioctl=%d %s (hw[0..1]=%02x %02x)\n\r",
                           (int)s, name[s], (int)rk,
                           (res & 1u) ? "MATCH-FIPS197 -> HW CRYPTO WORKS IN SPE" : "mismatch",
                           (unsigned)((res >> 8) & 0xff), (unsigned)((res >> 16) & 0xff));
            }
        }
    }

    /* HW AES-128-GCM (the QUIC record cipher) KAT + throughput in the SPE, vs the
     * software [MCB] number (465.59 cyc/B, 419 KB/s). Shows whether offloading GCM
     * to the IPSEC engine can lift the QUIC AES-GCM bottleneck. */
    {
        uint32_t g[4] = { 0, 0, 0, 0 };
        psa_outvec ov = { .base = g, .len = sizeof(g) };
        enum tfm_platform_err_t rg =
            tfm_platform_ioctl(AMEBAD_IOCTL_HWCRYPTO_GCM, NULL, &ov);
        DiagPrintf("[HWGCM] AES-128-GCM KAT(NIST): ioctl=%d %s (ct0=%02x tag0=%02x)\n\r",
                   (int)rg, (g[0] & 1u) ? "MATCH" : "MISMATCH",
                   (unsigned)((g[3] >> 24) & 0xff), (unsigned)((g[3] >> 8) & 0xff));
        if (g[2] > 0) {
            DiagPrintf("[HWGCM] HW throughput: %d.%02d cyc/B, %d KB/s   (SW [MCB]=465.59 cyc/B, 419 KB/s)\n\r",
                       (int)(g[1] / 100), (int)(g[1] % 100), (int)g[2]);
            DiagPrintf("[HWGCM] HW vs SW speedup: ~%d.%01dx\n\r",
                       (int)(g[2] * 10 / 419 / 10), (int)(g[2] * 10 / 419 % 10));
        } else {
            DiagPrintf("[HWGCM] throughput timer dead (cyc=0); DWT_CTRL=0x%08x (bit25=NOCYCCNT)\n\r", g[3]);
        }
    }
}
