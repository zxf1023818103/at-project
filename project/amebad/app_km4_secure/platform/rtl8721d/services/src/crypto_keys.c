/*
 * crypto_keys.c — RTL8721D crypto-keys HAL for TF-M (replaces the default
 * PLATFORM_DEFAULT_CRYPTO_KEYS template, ext/common/template/crypto_keys.c).
 *
 * ONLY difference vs the template: tfm_plat_get_huk() reads the device HUK from
 * the 32-byte TrustZone-secure user eFuse (0x150..0x16F) instead of the dummy,
 * NS-readable, flash-backed OTP. So the PS storage key (HKDF from the HUK) roots
 * in on-die OTP. (Today the eFuse is BLANK -> the HUK reads as a fixed value;
 * burning a per-device random HUK there makes PS device-unique with no code
 * change. Burning is a separate, irreversible manufacturing step.)
 *
 * The builtin-key policy/descriptor tables + the attestation-gated IAK loader are
 * verbatim from the template (attestation is OFF here, so the IAK paths compile
 * out). Enabled via -DAMEBAD_TFM_STORAGE -> config.cmake sets
 * PLATFORM_DEFAULT_CRYPTO_KEYS OFF and the platform CMake adds THIS file.
 */
#include <string.h>
#include "tfm_plat_crypto_keys.h"
#include "tfm_builtin_key_ids.h"
#include "tfm_plat_otp.h"
#include "psa_manifest/pid.h"
#include "tfm_builtin_key_loader.h"
#include "efuse_huk.h"

#define NUMBER_OF_ELEMENTS_OF(x) sizeof(x)/sizeof(*x)
#define MAPPED_TZ_NS_AGENT_DEFAULT_CLIENT_ID -0x3c000000
#define TFM_NS_PARTITION_ID                  MAPPED_TZ_NS_AGENT_DEFAULT_CLIENT_ID

static enum tfm_plat_err_t tfm_plat_get_huk(uint8_t *buf, size_t buf_len,
                                            size_t *key_len,
                                            psa_key_bits_t *key_bits,
                                            psa_algorithm_t *algorithm,
                                            psa_key_type_t *type)
{
    if (buf_len < AMEBAD_EFUSE_HUK_SIZE) {
        return TFM_PLAT_ERR_SYSTEM_ERR;
    }

    /* Device HUK = 32-byte TrustZone-secure user eFuse (on-die, NS cannot read). */
    amebad_secure_read_efuse(AMEBAD_EFUSE_HUK_ADDR, AMEBAD_EFUSE_HUK_SIZE, buf);

    *key_len   = AMEBAD_EFUSE_HUK_SIZE;
    *key_bits  = AMEBAD_EFUSE_HUK_SIZE * 8;
    *algorithm = PSA_ALG_HKDF(PSA_ALG_SHA_256);
    *type      = PSA_KEY_TYPE_DERIVE;

    return TFM_PLAT_ERR_SUCCESS;
}

#ifdef TFM_PARTITION_INITIAL_ATTESTATION
static enum tfm_plat_err_t tfm_plat_get_iak(uint8_t *buf, size_t buf_len,
                                            size_t *key_len,
                                            psa_key_bits_t *key_bits,
                                            psa_algorithm_t *algorithm,
                                            psa_key_type_t *type)
{
    enum tfm_plat_err_t err;
#ifndef SYMMETRIC_INITIAL_ATTESTATION
    psa_ecc_family_t curve_type;
#endif /* SYMMETRIC_INITIAL_ATTESTATION */

    err = tfm_plat_otp_read(PLAT_OTP_ID_IAK_LEN,
                            sizeof(size_t), (uint8_t*)key_len);
    if(err != TFM_PLAT_ERR_SUCCESS) {
        return err;
    }
    *key_bits = *key_len * 8;

    if (buf_len < *key_len) {
        return TFM_PLAT_ERR_SYSTEM_ERR;
    }

#ifdef SYMMETRIC_INITIAL_ATTESTATION
    err = tfm_plat_otp_read(PLAT_OTP_ID_IAK_TYPE,
                            sizeof(psa_algorithm_t), (uint8_t*)algorithm);
    if(err != TFM_PLAT_ERR_SUCCESS) {
        return err;
    }

    *type = PSA_KEY_TYPE_HMAC;
#else /* SYMMETRIC_INITIAL_ATTESTATION */
    err = tfm_plat_otp_read(PLAT_OTP_ID_IAK_TYPE, sizeof(psa_ecc_family_t),
                            &curve_type);
    if(err != TFM_PLAT_ERR_SUCCESS) {
        return err;
    }

    *algorithm = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
    *type = PSA_KEY_TYPE_ECC_KEY_PAIR(curve_type);
#endif /* SYMMETRIC_INITIAL_ATTESTATION */

    return tfm_plat_otp_read(PLAT_OTP_ID_IAK, *key_len, buf);
}
#endif /* TFM_PARTITION_INITIAL_ATTESTATION */

#ifdef TFM_PARTITION_INITIAL_ATTESTATION
static const tfm_plat_builtin_key_per_user_policy_t g_iak_per_user_policy[] = {
    {.user = TFM_SP_INITIAL_ATTESTATION,
#ifdef SYMMETRIC_INITIAL_ATTESTATION
        .usage = PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT,
#else
        .usage = PSA_KEY_USAGE_SIGN_HASH,
#endif /* SYMMETRIC_INITIAL_ATTESTATION */
    },
#ifdef TEST_S_ATTESTATION
    {.user = TFM_SP_SECURE_TEST_PARTITION, .usage = PSA_KEY_USAGE_VERIFY_HASH},
#endif /* TEST_S_ATTESTATION */
#ifdef TEST_NS_ATTESTATION
    {.user = TFM_NS_PARTITION_ID, .usage = PSA_KEY_USAGE_VERIFY_HASH},
#endif /* TEST_NS_ATTESTATION */
};
#endif /* TFM_PARTITION_INITIAL_ATTESTATION */

static const tfm_plat_builtin_key_policy_t g_builtin_keys_policy[] = {
    {.key_id = TFM_BUILTIN_KEY_ID_HUK, .per_user_policy = 0, .usage = PSA_KEY_USAGE_DERIVE},
#ifdef TFM_PARTITION_INITIAL_ATTESTATION
    {.key_id = TFM_BUILTIN_KEY_ID_IAK,
     .per_user_policy = NUMBER_OF_ELEMENTS_OF(g_iak_per_user_policy),
     .policy_ptr = g_iak_per_user_policy},
#endif /* TFM_PARTITION_INITIAL_ATTESTATION */
};

static const tfm_plat_builtin_key_descriptor_t g_builtin_keys_desc[] = {
    {.key_id = TFM_BUILTIN_KEY_ID_HUK,
     .slot_number = TFM_BUILTIN_KEY_SLOT_HUK,
     .lifetime = TFM_BUILTIN_KEY_LOADER_LIFETIME,
     .loader_key_func = tfm_plat_get_huk},
#ifdef TFM_PARTITION_INITIAL_ATTESTATION
    {.key_id = TFM_BUILTIN_KEY_ID_IAK,
     .slot_number = TFM_BUILTIN_KEY_SLOT_IAK,
     .lifetime = TFM_BUILTIN_KEY_LOADER_LIFETIME,
     .loader_key_func = tfm_plat_get_iak},
#endif /* TFM_PARTITION_INITIAL_ATTESTATION */
};

size_t tfm_plat_builtin_key_get_policy_table_ptr(const tfm_plat_builtin_key_policy_t *desc_ptr[])
{
    *desc_ptr = &g_builtin_keys_policy[0];
    return NUMBER_OF_ELEMENTS_OF(g_builtin_keys_policy);
}

size_t tfm_plat_builtin_key_get_desc_table_ptr(const tfm_plat_builtin_key_descriptor_t *desc_ptr[])
{
    *desc_ptr = &g_builtin_keys_desc[0];
    return NUMBER_OF_ELEMENTS_OF(g_builtin_keys_desc);
}
