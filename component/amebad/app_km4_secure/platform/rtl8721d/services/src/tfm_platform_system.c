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
/* eFuse security-key probe: read SBOOT public key (0x1A0,32B), RSIP key (0x190,16B) and the
 * protection/config region (0x1C0,2B) in the secure world (raw EFUSERead8 stalls from the NS
 * runtime), returning only NON-SECRET summaries {blank flag, folded fingerprint} + the (non-secret)
 * protection config word. Lets the OTA work confirm Secure Boot / RSIP keys are still unfused. */
#define AMEBAD_IOCTL_EFUSE_SEC_INFO  ((tfm_platform_ioctl_req_t)0x8721E1)
/* DEBUG / test-device key-inventory ONLY: return the RAW 32-byte HUK to NS. Deliberately defeats
 * the never-leave-the-SPE property above; for dev provisioning bookkeeping, NEVER ship. */
#define AMEBAD_IOCTL_EFUSE_HUK_RAW   ((tfm_platform_ioctl_req_t)0x8721E2)

/* HW-crypto KAT: run the IPSEC engine AES-128-ECB known-answer test from the
 * SECURE side. The crypto peripheral is security-attributed secure-only — the NS
 * driver-only image SecureFaults on it (see reference_amebad_hw_crypto); now that
 * we have TF-M, test whether the SPE can reach it. Step-based (step in in_vec) so
 * the NS client sees exactly which call dies if the engine hangs/faults:
 *   0 = CRYPTO_Init(NULL)   1 = rtl_crypto_aes_ecb_init(key,16)
 *   2 = rtl_crypto_aes_ecb_encrypt(pt) [prior NS death point]   3 = compare vs FIPS-197
 * ROM entry points are the SECURE aliases from rom_symbol_s.txt (called secure->
 * secure here, unlike the NS attempt). */
#define AMEBAD_IOCTL_HWCRYPTO_KAT    ((tfm_platform_ioctl_req_t)0x8721F0)
/* HW AES-128-GCM (QUIC record cipher) KAT + cyc/B throughput, run in the SPE. */
#define AMEBAD_IOCTL_HWCRYPTO_GCM    ((tfm_platform_ioctl_req_t)0x872200)
/* One 1200-byte GCM op (QUIC packet size) — step 0=init(key), 1=encrypt+time.
 * Lets NS measure the full per-packet NS->SPE round-trip vs the SPE-internal op. */
#define AMEBAD_IOCTL_HWCRYPTO_GCM1200 ((tfm_platform_ioctl_req_t)0x872210)

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
    case AMEBAD_IOCTL_EFUSE_HUK_RAW: {
        /* DEBUG / test-device key inventory ONLY: copy the raw 32-byte HUK (secure eFuse 0x150)
         * to the NS output buffer. Exposes the device root key - never ship. */
        if (out_vec == NULL || out_vec->base == NULL || out_vec->len < AMEBAD_EFUSE_HUK_SIZE) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        amebad_secure_read_efuse(AMEBAD_EFUSE_HUK_ADDR, AMEBAD_EFUSE_HUK_SIZE,
                                 (uint8_t *)out_vec->base);
        out_vec->len = AMEBAD_EFUSE_HUK_SIZE;
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    case AMEBAD_IOCTL_EFUSE_SEC_INFO: {
        /* {sboot_blank, sboot_fp, rsip_blank, rsip_fp, prot} — all non-secret summaries. */
        uint8_t  buf[32];
        uint32_t out[5];
        uint32_t i, fp;
        int all_ff, all_00;

        if (out_vec == NULL || out_vec->base == NULL || out_vec->len < sizeof(out)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        /* SBOOT public key (Ed25519) @ 0x1A0, 32 B */
        amebad_secure_read_efuse(0x1A0u, 32u, buf);
        fp = 0x811C9DC5u; all_ff = 1; all_00 = 1;
        for (i = 0; i < 32u; i++) {
            fp = (fp ^ buf[i]) * 0x01000193u;
            if (buf[i] != 0xFFu) { all_ff = 0; }
            if (buf[i] != 0x00u) { all_00 = 0; }
        }
        out[0] = (uint32_t)((all_ff || all_00) ? 1 : 0);
        out[1] = fp;
        /* RSIP key @ 0x190, 16 B */
        amebad_secure_read_efuse(0x190u, 16u, buf);
        fp = 0x811C9DC5u; all_ff = 1; all_00 = 1;
        for (i = 0; i < 16u; i++) {
            fp = (fp ^ buf[i]) * 0x01000193u;
            if (buf[i] != 0xFFu) { all_ff = 0; }
            if (buf[i] != 0x00u) { all_00 = 0; }
        }
        out[2] = (uint32_t)((all_ff || all_00) ? 1 : 0);
        out[3] = fp;
        /* protection / security-config @ 0x1C0, 2 B (non-secret config) */
        amebad_secure_read_efuse(0x1C0u, 2u, buf);
        out[4] = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
        memset(buf, 0, sizeof(buf));

        memcpy(out_vec->base, out, sizeof(out));
        out_vec->len = sizeof(out);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    case AMEBAD_IOCTL_HWCRYPTO_KAT: {
        typedef int (*crypto_init_fn)(void *);
        typedef int (*aes_init_fn)(const unsigned char *, unsigned int);
        typedef int (*aes_ecb_fn)(const unsigned char *, unsigned int,
                                  const unsigned char *, unsigned int, unsigned char *);
        /* SECURE ROM aliases (rom_symbol_s.txt: secure ROM 0x10104xxx). */
        crypto_init_fn ROM_CRYPTO_Init     = (crypto_init_fn)0x10104a49u;
        aes_init_fn    ROM_aes_ecb_init    = (aes_init_fn)0x10103d19u;
        aes_ecb_fn     ROM_aes_ecb_encrypt = (aes_ecb_fn)0x10103d69u;
        static const unsigned char exp[16] = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
            0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
        /* The IPSEC engine DMA-reads key/message — they must live in SRAM, NOT in
         * the secure XIP flash (.rodata). Use static .bss (SRAM) so they also persist
         * across the separate step IOCTL calls (aes_ecb_init saves the key pointer,
         * loaded later by aes_ecb_encrypt). */
        static unsigned char k[16], pt[16];
        static unsigned char hw[16];   /* .bss = SRAM (engine DMA-writes here) */
        uint32_t step, res = 0, i;

        if (in_vec == NULL || in_vec->base == NULL || in_vec->len != sizeof(uint32_t) ||
            out_vec == NULL || out_vec->base == NULL || out_vec->len < sizeof(uint32_t)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        for (i = 0; i < 16; i++) { k[i] = (unsigned char)i; pt[i] = (unsigned char)((i << 4) | i); }
        memcpy(&step, in_vec->base, sizeof(step));

        switch (step) {
        case 0: res = (uint32_t)ROM_CRYPTO_Init(0); break;                  /* touch secure-only periph */
        case 1: res = (uint32_t)ROM_aes_ecb_init(k, 16); break;
        case 2: res = (uint32_t)ROM_aes_ecb_encrypt(pt, 16, 0, 0, hw); break; /* prior NS death point */
        case 3:                                                            /* compare vs FIPS-197 */
            res  = (memcmp(hw, exp, 16) == 0) ? 1u : 0u;
            res |= (uint32_t)hw[0] << 8;
            res |= (uint32_t)hw[1] << 16;
            break;
        default:
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        memcpy(out_vec->base, &res, sizeof(res));
        out_vec->len = sizeof(res);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    case AMEBAD_IOCTL_HWCRYPTO_GCM: {
        /* HW AES-128-GCM (the QUIC record cipher) from the SPE: KAT + cyc/B via the
         * DWT cycle counter, to compare vs the software [MCB] number (465 cyc/B,
         * 419 KB/s). All buffers in SRAM .bss (the IPSEC DMA can't read flash). */
        typedef int (*gcm_init_fn)(const unsigned char *, unsigned int);
        typedef int (*gcm_enc_fn)(const unsigned char *msg, unsigned int msglen,
                                  const unsigned char *iv, const unsigned char *aad,
                                  unsigned int aadlen, unsigned char *out, unsigned char *tag);
        gcm_init_fn ROM_gcm_init = (gcm_init_fn)0x10104065u;   /* secure ROM */
        gcm_enc_fn  ROM_gcm_enc  = (gcm_enc_fn)0x101040b9u;
        /* NIST GCM AES-128 test case 2: all-zero key/IV/PT, no AAD. */
        static const unsigned char exp_ct[16] = {
            0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78 };
        static const unsigned char exp_tag[16] = {
            0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf };
        static unsigned char zk[16], ziv[12], zpt[16], ctk[16], tag[16], iv[12];
        static unsigned char buf[4096], obuf[4096];
        uint32_t res[4] = {0,0,0,0};
        uint32_t i, n, c0, c1, cyc, total, kat;
        const uint32_t HZ = 200000000u;
        const uint32_t LOOPS = 64u;        /* 64 * 4 KB = 256 KB through GCM */

        if (out_vec == NULL || out_vec->base == NULL || out_vec->len < sizeof(res)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        for (i = 0; i < 16; i++) { zk[i] = 0; zpt[i] = 0; }
        for (i = 0; i < 12; i++) { ziv[i] = 0; iv[i] = (unsigned char)i; }

        /* ---- KAT (correctness) ---- */
        ROM_gcm_init(zk, 16);
        ROM_gcm_enc(zpt, 16, ziv, 0, 0, ctk, tag);
        kat = (memcmp(ctk, exp_ct, 16) == 0 && memcmp(tag, exp_tag, 16) == 0) ? 1u : 0u;

        /* ---- throughput (cyc/B over 256 KB) ---- */
        for (i = 0; i < sizeof(buf); i++) { buf[i] = (unsigned char)i; }
        ROM_gcm_init(zk, 16);
        /* This M33 has no DWT CYCCNT (NOCYCCNT=1) -> use SysTick. A single 4 KB GCM
         * op is well under the 24-bit (16.7M) wrap, so measure per-op and accumulate.
         * Save/restore SysTick so TF-M timing is undisturbed. */
        {
            volatile uint32_t *SYST_CSR = (volatile uint32_t *)0xE000E010u;
            volatile uint32_t *SYST_RVR = (volatile uint32_t *)0xE000E014u;
            volatile uint32_t *SYST_CVR = (volatile uint32_t *)0xE000E018u;
            uint32_t save_csr = *SYST_CSR, save_rvr = *SYST_RVR;
            cyc = 0;
            *SYST_CSR = 0;                 /* stop */
            *SYST_RVR = 0x00FFFFFFu;       /* 24-bit max */
            *SYST_CVR = 0;                 /* clear -> reloads to RVR */
            *SYST_CSR = 5u;                /* ENABLE | CLKSOURCE=processor */
            for (n = 0; n < LOOPS; n++) {
                uint32_t s = *SYST_CVR;
                ROM_gcm_enc(buf, sizeof(buf), iv, 0, 0, obuf, tag);
                cyc += (s - *SYST_CVR) & 0x00FFFFFFu;   /* down-counter delta (1 op << 16M) */
            }
            *SYST_CSR = 0; *SYST_RVR = save_rvr; *SYST_CVR = 0; *SYST_CSR = save_csr;
        }
        total = LOOPS * (uint32_t)sizeof(buf);
        res[0] = kat;
        res[1] = (cyc != 0) ? (uint32_t)((unsigned long long)cyc * 100u / total) : 0u;  /* cyc/B x100 */
        res[2] = (cyc != 0) ? (uint32_t)((unsigned long long)total * HZ / cyc / 1024u) : 0u; /* KB/s */
        res[3] = ((uint32_t)ctk[0] << 24) | ((uint32_t)ctk[1] << 16) |
                 ((uint32_t)tag[0] << 8) | (uint32_t)tag[1];
        (void)c0; (void)c1;
        memcpy(out_vec->base, res, sizeof(res));
        out_vec->len = sizeof(res);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    case AMEBAD_IOCTL_HWCRYPTO_GCM1200: {
        typedef int (*gcm_init_fn)(const unsigned char *, unsigned int);
        typedef int (*gcm_enc_fn)(const unsigned char *, unsigned int, const unsigned char *,
                                  const unsigned char *, unsigned int, unsigned char *, unsigned char *);
        gcm_init_fn ROM_gcm_init = (gcm_init_fn)0x10104065u;
        gcm_enc_fn  ROM_gcm_enc  = (gcm_enc_fn)0x101040b9u;
        static unsigned char zk[16], iv[12], pbuf[1200], pout[1200], ptag[16];
        volatile uint32_t *SYST_CSR = (volatile uint32_t *)0xE000E010u;
        volatile uint32_t *SYST_RVR = (volatile uint32_t *)0xE000E014u;
        volatile uint32_t *SYST_CVR = (volatile uint32_t *)0xE000E018u;
        uint32_t step, res = 0, i;

        if (in_vec == NULL || in_vec->base == NULL || in_vec->len != sizeof(uint32_t) ||
            out_vec == NULL || out_vec->base == NULL || out_vec->len < sizeof(uint32_t)) {
            return TFM_PLATFORM_ERR_INVALID_PARAM;
        }
        memcpy(&step, in_vec->base, sizeof(step));

        if (step == 0) {                /* load key once (QUIC keeps the key across packets) */
            for (i = 0; i < 16; i++) { zk[i] = 0; }
            for (i = 0; i < 12; i++) { iv[i] = (unsigned char)i; }
            for (i = 0; i < sizeof(pbuf); i++) { pbuf[i] = (unsigned char)i; }
            ROM_gcm_init(zk, 16);
            res = 0;
        } else {                        /* time ONE 1200B encrypt with the secure SysTick */
            uint32_t scsr = *SYST_CSR, srvr = *SYST_RVR, s, e;
            *SYST_CSR = 0; *SYST_RVR = 0x00FFFFFFu; *SYST_CVR = 0; *SYST_CSR = 5u;
            s = *SYST_CVR;
            ROM_gcm_enc(pbuf, sizeof(pbuf), iv, 0, 0, pout, ptag);
            e = *SYST_CVR;
            *SYST_CSR = 0; *SYST_RVR = srvr; *SYST_CVR = 0; *SYST_CSR = scsr;
            res = (s - e) & 0x00FFFFFFu;            /* SPE-internal 1200B encrypt cycles */
        }
        memcpy(out_vec->base, &res, sizeof(res));
        out_vec->len = sizeof(res);
        return TFM_PLATFORM_ERR_SUCCESS;
    }
    default:
        return TFM_PLATFORM_ERR_NOT_SUPPORTED;
    }
}
