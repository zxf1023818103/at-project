/*
 * aws_provision.h — credential storage for the AmebaD KM4 AWS IoT demo.
 *
 *   - device name  = the module BLE MAC, read live from efuse (0x190). It is the
 *                    MQTT client id / AWS Thing name.
 *   - endpoint     = AWS IoT ATS endpoint  -> TF-M Protected Storage.
 *   - client cert  = X.509 client cert PEM -> TF-M Protected Storage.
 *   - private key  = client key PEM        -> TF-M Protected Storage (secret).
 *
 * All three live in TF-M PS (encrypted at rest). LittleFS is avoided: its
 * XIP-pinned .text hangs in this image's layout (see aws_provision.c).
 *
 * Provisioned at runtime via the `set <endpoint|cert|key> <value>` console command
 * (aws_cli.c). All functions return 0 on success, negative on error.
 *
 * Loaded PEM buffers are NUL-terminated and the returned length INCLUDES the NUL,
 * as mbedTLS PEM parsing (mbedtls_x509_crt_parse / mbedtls_pk_parse_key) requires.
 */
#ifndef AWS_PROVISION_H
#define AWS_PROVISION_H

#include <stddef.h>

/* Max provisioned sizes. AWS-issued RSA-2048 cert/key PEMs are ~1.2/1.7 KB. */
#define AWS_ENDPOINT_MAX   128
#define AWS_CERT_MAX       2560
#define AWS_KEY_MAX        2560

/* Create the PSA-access mutex. Call once, pre-scheduler (single-threaded). */
void aws_provision_init(void);

/* BLE MAC as 12 lowercase hex chars + NUL (e.g. "112233aabbcc"). out must be >=13. */
int aws_get_ble_mac(char *out, size_t out_len);

/* Endpoint (plain string) -> LittleFS. */
int aws_store_endpoint(const char *endpoint);
int aws_load_endpoint(char *buf, size_t buf_len);   /* NUL-terminated */

/* Client certificate PEM -> LittleFS. */
int aws_store_cert(const char *pem, size_t len);
int aws_load_cert(char *buf, size_t buf_len, size_t *out_len);   /* out_len includes NUL */

/* Client private key PEM -> TF-M Protected Storage (secure). */
int aws_store_key(const char *pem, size_t len);
int aws_load_key(char *buf, size_t buf_len, size_t *out_len);    /* out_len includes NUL */

/* 1 if endpoint + cert + key are all present, else 0. */
int aws_is_provisioned(void);

#endif /* AWS_PROVISION_H */
