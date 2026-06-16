/*
 * Copyright (c) 2021-2022, Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "tfm_plat_crypto_nv_seed.h"
#include "tfm_plat_otp.h"
#include "psa/internal_trusted_storage.h"
#include "config_tfm.h"

#define NV_SEED_SIZE 64
#if NV_SEED_SIZE > ITS_MAX_ASSET_SIZE
#error "ITS_MAX_ASSET_SIZE is too small to store the entropy seed"
#endif

int tfm_plat_crypto_provision_entropy_seed(void)
{
    enum tfm_plat_err_t plat_err;
    int err;
    uint8_t buf[NV_SEED_SIZE];

    /* If the seed is already provisioned, then return successfully */
    err = tfm_plat_crypto_nv_seed_read(buf, sizeof(buf));
    if (err == TFM_CRYPTO_NV_SEED_SUCCESS) {
        return err;
    }

    plat_err = tfm_plat_otp_read(PLAT_OTP_ID_ENTROPY_SEED, sizeof(buf), buf);
    if(plat_err != TFM_PLAT_ERR_SUCCESS) {
        return TFM_CRYPTO_NV_SEED_FAILED;
    }

    err = tfm_plat_crypto_nv_seed_write(buf, sizeof(buf));
    if (err != TFM_CRYPTO_NV_SEED_SUCCESS) {
        return err;
    }

    return TFM_CRYPTO_NV_SEED_SUCCESS;
}

int tfm_plat_crypto_nv_seed_read(unsigned char *buf, size_t buf_len)
{
    psa_storage_uid_t uid = NV_SEED_FILE_ID;
    psa_status_t status;
    size_t data_length = 0;

    status = psa_its_get(uid, 0, buf_len, buf, &data_length);

    if (status == PSA_SUCCESS && data_length == buf_len) {
        return TFM_CRYPTO_NV_SEED_SUCCESS;
    } else {
        return TFM_CRYPTO_NV_SEED_FAILED;
    }
}

int tfm_plat_crypto_nv_seed_write(const unsigned char *buf, size_t buf_len)
{
    psa_storage_uid_t uid = NV_SEED_FILE_ID;
    psa_status_t status;
    struct psa_storage_info_t info;

    /* AmebaD (RTL8721D) dual-core hazard: rewriting the NV seed on every boot (mbedTLS's
     * forward-secrecy seed update at DRBG seeding) triggers an ITS block GC. When a large
     * object shares that block (a provisioned AWS key/cert), the GC copies it as long 256-byte
     * page programs whose wide SPIC user-mode window corrupts KM0's concurrent XIP fetches
     * (AN0400 §18.1) -> KM0 boot HardFault/hang. So persist only the FIRST (provisioning)
     * write; skip the per-boot update once a seed exists. Trade-off: the DRBG re-seeds from a
     * fixed NV seed each boot (weaker forward secrecy) — acceptable for this demo; a production
     * fix needs a HW TRNG or KM0 IPC-quiesce during the SPE flash write. */
    if (psa_its_get_info(uid, &info) == PSA_SUCCESS) {
        return TFM_CRYPTO_NV_SEED_SUCCESS;
    }

    status = psa_its_set(uid, buf_len, buf, 0);

    if (status == PSA_SUCCESS) {
        return TFM_CRYPTO_NV_SEED_SUCCESS;
    } else {
        return TFM_CRYPTO_NV_SEED_FAILED;
    }
}
