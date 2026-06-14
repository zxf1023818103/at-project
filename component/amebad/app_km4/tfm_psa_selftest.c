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
#define AMEBAD_IOCTL_HWCRYPTO_GCM1200 ((tfm_platform_ioctl_req_t)0x872210)

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

#ifndef AT_PROJECT_MINIMAL
    /* The HW-crypto KAT/GCM throughput bench below is a TF-M diagnostic, not part
     * of the WiFi-connect proof — dropped from the minimal image. The PSA ping +
     * eFuse-HUK probe above stay (they are the "TF-M is alive" evidence). */

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

    /* Per-packet (1200 B) HW-GCM cost INCLUDING the NS->SPE PSA round-trip — the
     * real cost QUIC would pay per packet. NS SysTick times the full ioctl; the SPE
     * returns its internal encrypt-only cycles so we can split out the boundary. */
    {
        volatile uint32_t *CSR = (volatile uint32_t *)0xE000E010u;   /* NS SysTick */
        volatile uint32_t *RVR = (volatile uint32_t *)0xE000E014u;
        volatile uint32_t *CVR = (volatile uint32_t *)0xE000E018u;
        uint32_t save_csr, save_rvr, k, init = 0, one = 1, spe = 0;
        uint32_t ns_total = 0, spe_last = 0;
        const uint32_t M = 64u, HZ = 200000000u, PKT = 1200u;
        psa_invec  iv0 = { .base = &init, .len = sizeof(init) };
        psa_outvec ov0 = { .base = &spe,  .len = sizeof(spe)  };

        tfm_platform_ioctl(AMEBAD_IOCTL_HWCRYPTO_GCM1200, &iv0, &ov0);   /* load key once */

        save_csr = *CSR; save_rvr = *RVR;
        *CSR = 0; *RVR = 0x00FFFFFFu; *CVR = 0; *CSR = 5u;               /* NS SysTick, CPU clk */
        for (k = 0; k < M; k++) {
            uint32_t sp = 0, s;
            psa_invec  ive = { .base = &one, .len = sizeof(one) };
            psa_outvec ove = { .base = &sp,  .len = sizeof(sp)  };
            s = *CVR;
            tfm_platform_ioctl(AMEBAD_IOCTL_HWCRYPTO_GCM1200, &ive, &ove);
            ns_total += (s - *CVR) & 0x00FFFFFFu;                        /* full round-trip cyc */
            spe_last = sp;
        }
        *CSR = 0; *RVR = save_rvr; *CVR = 0; *CSR = save_csr;

        {
            uint32_t ns_per = ns_total / M;                 /* cyc per packet (round-trip) */
            uint32_t boundary = (ns_per > spe_last) ? (ns_per - spe_last) : 0u;
            DiagPrintf("[GCM1200] SPE-internal 1200B op : %d cyc (%d.%02d cyc/B)\n\r",
                       (int)spe_last, (int)(spe_last / PKT), (int)((spe_last * 100u / PKT) % 100));
            DiagPrintf("[GCM1200] NS->SPE round-trip/pkt: %d cyc (%d.%02d cyc/B); PSA boundary=%d cyc\n\r",
                       (int)ns_per, (int)(ns_per / PKT), (int)((ns_per * 100u / PKT) % 100), (int)boundary);
            DiagPrintf("[GCM1200] per-packet HW-GCM throughput: %d KB/s   (SW [MCB]=419 KB/s)\n\r",
                       (int)(ns_per ? ((unsigned long long)PKT * HZ / ns_per / 1024u) : 0u));
        }
    }

    /* Can the NSPE call HW crypto DIRECTLY (no PSA round-trip)? target_cfg's SAU
     * marks the crypto NS alias (0x40022000) as NS, but the AmebaD bus-level slave
     * security gate (PPC/TZC) is NOT configured by TF-M. Test empirically: call the
     * NS ROM crypto aliases from the NS world. If the slave gate blocks NS this
     * BusFaults/hangs (prior no-TF-M finding); if it allows NS, the KAT MATCHes and
     * we can skip the SPE entirely. Step prints show exactly where it dies. Run LAST
     * so the IOCTL results above are already out if this faults pre-scheduler. */
    {
        typedef int (*crypto_init_fn)(void *);
        typedef int (*aes_init_fn)(const unsigned char *, unsigned int);
        typedef int (*aes_ecb_fn)(const unsigned char *, unsigned int,
                                  const unsigned char *, unsigned int, unsigned char *);
        crypto_init_fn NS_CRYPTO_Init     = (crypto_init_fn)0x1010ea49u;   /* NS ROM aliases */
        aes_init_fn    NS_aes_ecb_init    = (aes_init_fn)0x1010dd19u;
        aes_ecb_fn     NS_aes_ecb_encrypt = (aes_ecb_fn)0x1010dd69u;
        static unsigned char k[16], pt[16], hw[16];
        static const unsigned char exp[16] = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
            0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
        int r; uint32_t i;
        for (i = 0; i < 16; i++) { k[i] = (unsigned char)i; pt[i] = (unsigned char)((i << 4) | i); }
        DiagPrintf("[HWNS] NSPE-direct HW crypto via NS ROM aliases (no SPE/PSA)...\n\r");
        DiagPrintf("[HWNS] CRYPTO_Init...\n\r");
        r = NS_CRYPTO_Init(0);
        DiagPrintf("[HWNS]   CRYPTO_Init=%d (survived)\n\r", r);
        DiagPrintf("[HWNS] aes_ecb_init...\n\r");
        r = NS_aes_ecb_init(k, 16);
        DiagPrintf("[HWNS]   aes_ecb_init=%d (survived)\n\r", r);
        DiagPrintf("[HWNS] aes_ecb_encrypt... (prior NS death point)\n\r");
        r = NS_aes_ecb_encrypt(pt, 16, 0, 0, hw);
        DiagPrintf("[HWNS]   aes_ecb_encrypt=%d (SURVIVED) hw[0..1]=%02x %02x -> %s\n\r",
                   r, hw[0], hw[1],
                   memcmp(hw, exp, 16) == 0 ? "MATCH (NSPE CAN call HW crypto directly!)" : "mismatch");
    }
#endif /* !AT_PROJECT_MINIMAL */
}
