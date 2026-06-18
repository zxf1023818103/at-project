/*
 * tfm_storage_selftest.c — app_km4 (NSPE) exercises TF-M PSA secure storage.
 *
 * When AMEBAD_TFM_STORAGE is on, the TF-M SPE has the Internal Trusted Storage
 * (ITS) partition [+ Protected Storage (PS) in the crypto-enabled build]. This NS
 * self-test proves the round-trip AND persistence across reboot, backed by the
 * real AmebaD flash driver (app_km4_secure/.../cmsis_drivers/Driver_Flash.c):
 *
 *   - First boot: the ITS UID doesn't exist -> psa_its_set(secret), read back,
 *     verify -> "[STG] ITS set/get PASS (first boot)".
 *   - Later boots: the UID exists -> read back, compare to the known secret ->
 *     "[STG] ITS persistence PASS (survived reboot)".
 *
 * Called early from main() pre-scheduler (single-threaded -> the bare-metal PSA
 * dispatch is safe), like tfm_psa_selftest.c.
 */
#include <stdint.h>
#include "at_log.h"
#include <string.h>

#include "psa/internal_trusted_storage.h"
#ifdef AMEBAD_TFM_STORAGE_PS
#include "psa/protected_storage.h"
#endif


#define STG_ITS_UID     ((psa_storage_uid_t)0x5354475F49545301ULL) /* "STG_ITS\1" */
#define STG_PS_UID      ((psa_storage_uid_t)0x5354475F50530001ULL) /* "STG_PS\0\1" */

/* Known secret written on first boot; checked on every boot. */
static const uint8_t stg_secret[32] = {
    0xA5, 0x5A, 0xC3, 0x3C, 0x01, 0x23, 0x45, 0x67,
    0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98,
    0x76, 0x54, 0x32, 0x10, 0x0F, 0x1E, 0x2D, 0x3C,
    0x4B, 0x5A, 0x69, 0x78, 0x87, 0x96, 0xA5, 0xB4,
};

static void stg_test_its(void)
{
    struct psa_storage_info_t info;
    uint8_t  rd[sizeof(stg_secret)];
    size_t   got = 0;
    psa_status_t s;

    s = psa_its_get_info(STG_ITS_UID, &info);
    if (s == PSA_ERROR_DOES_NOT_EXIST) {
        /* First boot: write the secret. */
        s = psa_its_set(STG_ITS_UID, sizeof(stg_secret), stg_secret, PSA_STORAGE_FLAG_NONE);
        if (s != PSA_SUCCESS) {
            log_error("[STG] ITS set FAIL (status=%d)", (int)s);
            return;
        }
        memset(rd, 0, sizeof(rd));
        s = psa_its_get(STG_ITS_UID, 0, sizeof(rd), rd, &got);
        log_info("[STG] ITS set/get %s (first boot, status=%d len=%d match=%d)",
                   (s == PSA_SUCCESS && got == sizeof(stg_secret) &&
                    memcmp(rd, stg_secret, sizeof(stg_secret)) == 0) ? "PASS" : "FAIL",
                   (int)s, (int)got, (int)(memcmp(rd, stg_secret, sizeof(stg_secret)) == 0));
        return;
    }
    if (s != PSA_SUCCESS) {
        log_error("[STG] ITS get_info FAIL (status=%d)", (int)s);
        return;
    }

    /* Subsequent boot: the UID persisted -> read back and compare. */
    memset(rd, 0, sizeof(rd));
    s = psa_its_get(STG_ITS_UID, 0, sizeof(rd), rd, &got);
    log_info("[STG] ITS persistence %s (survived reboot, size=%d len=%d)",
               (s == PSA_SUCCESS && got == sizeof(stg_secret) &&
                memcmp(rd, stg_secret, sizeof(stg_secret)) == 0) ? "PASS" : "FAIL",
               (int)info.size, (int)got);
}

#ifdef AMEBAD_TFM_STORAGE_PS
static void stg_test_ps(void)
{
    struct psa_storage_info_t info;
    uint8_t  rd[sizeof(stg_secret)];
    size_t   got = 0;
    psa_status_t s;
    /* Raw PS flash region (NS-readable alias): used to prove encryption-at-rest. */
    const volatile uint8_t *ps_raw = (const volatile uint8_t *)(0x08000000u + 0x3F4000u);

    s = psa_ps_get_info(STG_PS_UID, &info);
    if (s == PSA_ERROR_DOES_NOT_EXIST) {
        s = psa_ps_set(STG_PS_UID, sizeof(stg_secret), stg_secret, PSA_STORAGE_FLAG_NONE);
        if (s != PSA_SUCCESS) { log_error("[STG] PS set FAIL (status=%d)", (int)s); return; }
        memset(rd, 0, sizeof(rd));
        s = psa_ps_get(STG_PS_UID, 0, sizeof(rd), rd, &got);
        log_info("[STG] PS set/get %s (first boot, status=%d len=%d)",
                   (s == PSA_SUCCESS && got == sizeof(stg_secret) &&
                    memcmp(rd, stg_secret, sizeof(stg_secret)) == 0) ? "PASS" : "FAIL",
                   (int)s, (int)got);
    } else if (s == PSA_SUCCESS) {
        memset(rd, 0, sizeof(rd));
        s = psa_ps_get(STG_PS_UID, 0, sizeof(rd), rd, &got);
        log_info("[STG] PS persistence %s (survived reboot, len=%d)",
                   (s == PSA_SUCCESS && got == sizeof(stg_secret) &&
                    memcmp(rd, stg_secret, sizeof(stg_secret)) == 0) ? "PASS" : "FAIL",
                   (int)got);
    } else {
        log_error("[STG] PS get_info FAIL (status=%d)", (int)s); return;
    }

    /* Confidentiality: the raw PS flash bytes must NOT equal the plaintext secret
     * (PS encrypts at rest; ITS would store plaintext). */
    {
        int leaks_plaintext = 1;
        for (unsigned i = 0; i < sizeof(stg_secret); i++) {
            if (ps_raw[i] != stg_secret[i]) { leaks_plaintext = 0; break; }
        }
        log_info("[STG] PS ciphertext-at-rest %s (raw[0..3]=%02x %02x %02x %02x)",
                   leaks_plaintext ? "FAIL" : "PASS",
                   ps_raw[0], ps_raw[1], ps_raw[2], ps_raw[3]);
    }
}
#endif /* AMEBAD_TFM_STORAGE_PS */

void amebad_tfm_storage_selftest(void)
{
    log_info("[STG] PSA secure storage self-test");
    stg_test_its();
#ifdef AMEBAD_TFM_STORAGE_PS
    stg_test_ps();
#endif
}
