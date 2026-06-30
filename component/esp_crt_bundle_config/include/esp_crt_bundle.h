/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileContributor: 2026 at-project AmebaD port
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP-IDF x509 certificate-bundle API, ported to AmebaD (RTL8721D) / Mbed TLS v3.6.4.
 *
 * The bundle embeds the full Mozilla NSS root store in a COMPRESSED form: each root is
 * reduced to just its DER subject name + DER public key (SubjectPublicKeyInfo) -- the only
 * two things needed to confirm a chain's top cert was signed by a trusted root -- in a flat,
 * offset-indexed, subject-sorted blob (binary-searched by issuer name). ~121 roots fit in
 * ~56 KB of .rodata instead of ~190 KB of PEM.
 *
 * Unlike upstream ESP-IDF this port uses the classic (non-PSA) Mbed TLS verify path and drops
 * the cross-signed branch -- see component/esp_crt_bundle_config/esp_crt_bundle.c.
 */
#ifndef _ESP_CRT_BUNDLE_H_
#define _ESP_CRT_BUNDLE_H_

#include <stdint.h>
#include <stdbool.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attach the embedded CA bundle to an mbedTLS SSL config for server verification.
 *
 * Installs a verification callback (esp_crt_verify_callback) so the server cert is checked against
 * the embedded bundle, and sets the CA chain to @p trust_ca (the file/folder-loaded extra roots,
 * e.g. the `ca/` folder) so a cert that chains to one of those is also accepted -- bundle +
 * file-folder coverage. If @p trust_ca is NULL a dummy chain is used (bundle only). Used by the
 * mbedTLS transport backend.
 *
 * @param[in] conf      Pointer to the mbedtls_ssl_config for the connection.
 * @param[in] trust_ca  Extra file-loaded trust anchors (may be empty or NULL).
 * @return 0 on success, non-zero if the embedded bundle failed its consistency check.
 */
int esp_crt_bundle_attach(void *conf, mbedtls_x509_crt *trust_ca);

/**
 * @brief Remove the bundle verification callback + dummy chain from an mbedTLS SSL config.
 *
 * @param[in] conf  Pointer to the mbedtls_ssl_config (may be NULL).
 */
void esp_crt_bundle_detach(void *conf);

/**
 * @brief Override the default embedded bundle (e.g. an OTA-updated bundle in RAM/flash).
 *
 * The data must outlive its use and be sorted by subject name (binary search).
 *
 * @param[in] x509_bundle  Pointer to the bundle blob.
 * @param[in] bundle_size  Size of the blob in bytes.
 * @return 0 on success, non-zero if the bundle failed its consistency check.
 */
int esp_crt_bundle_set(const uint8_t *x509_bundle, size_t bundle_size);

/**
 * @brief Per-certificate verify callback (the f_vrfy used by both the mbedTLS conf_verify hook
 *        and mbedtls_x509_crt_verify()). Looks the chain's untrusted top cert up in the bundle
 *        by issuer name and verifies its signature against the matching root's public key.
 */
int esp_crt_verify_callback(void *buf, mbedtls_x509_crt *crt, int depth, uint32_t *flags);

/**
 * @brief Verify a parsed server chain against the embedded bundle (convenience for the picotls
 *        backend, which runs its own mbedtls_x509_crt_verify()). Ensures the bundle is loaded,
 *        then verifies with the dummy CA chain + esp_crt_verify_callback.
 *
 * @param[in]  chain     The parsed server certificate chain (leaf = list head).
 * @param[in]  trust_ca  Extra file-loaded trust anchors tried alongside the bundle (may be NULL).
 * @param[in]  cn        Expected server hostname (or NULL to skip the CN check).
 * @param[out] flags     Verification result flags (as mbedtls_x509_crt_verify()).
 * @return 0 if the chain verifies against a bundle root or one of @p trust_ca, non-zero otherwise.
 */
int esp_crt_bundle_verify_chain(mbedtls_x509_crt *chain, mbedtls_x509_crt *trust_ca,
                                const char *cn, uint32_t *flags);

/**
 * @brief Number of root certificates in the active bundle (0 if not yet loaded / invalid).
 *        Loads the embedded bundle on first call. Used by the `crt_bundle info` console command.
 */
uint32_t esp_crt_bundle_count(void);

/**
 * @brief Size in bytes of the embedded bundle blob (the compressed .rodata footprint).
 */
uint32_t esp_crt_bundle_blob_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _ESP_CRT_BUNDLE_H_ */
