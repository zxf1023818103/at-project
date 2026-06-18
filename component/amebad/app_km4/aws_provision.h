/*
 * aws_provision.h — credential storage for the AmebaD KM4 AWS IoT demo.
 *
 *   - device name  = the module BLE MAC, read live from efuse (0x190). It is the
 *                    MQTT client id / AWS Thing name.
 *   - endpoint     = AWS IoT ATS endpoint  -> env var "endpoint" (KV/endpoint).
 *   - client cert  = X.509 client cert     -> plain LittleFS DER file (aws_cert.der),
 *                    its path published as env var "cert" (KV/cert).
 *   - private key  = client key            -> TF-M Internal Trusted Storage (secret).
 *
 * Split by secrecy: the endpoint and cert are not secret, so they live in plain
 * LittleFS (the endpoint as an environment variable, the cert as a DER file the TLS
 * transport loads with mbedtls_x509_crt_parse_file). Only the private key needs the
 * encrypted / TrustZone-protected TF-M ITS. There is NO embedded fallback: an
 * unprovisioned device idle-retries instead of connecting.
 *
 * Provisioned at runtime via the `set <endpoint|cert|key> <value>` console command
 * (aws_cli.c). All functions return 0 on success, negative on error.
 *
 * The loaded key buffer is the DER bytes (exact length in *out_len);
 * mbedtls_pk_parse_key auto-detects DER vs PEM.
 */
#ifndef AWS_PROVISION_H
#define AWS_PROVISION_H

#include <stddef.h>

/* Max provisioned sizes. An AWS-issued RSA-2048 cert DER is ~0.9 KB and the key DER ~1.2 KB.
 * Sized 1536 (not 2560) deliberately: these back static .bss scratch buffers (aws_provision.c
 * s_der/vbuf, aws_iot_demo.c key[]), and the FreeRTOS heap is everything from __bss_end__ to the
 * TF-M secure-RAM top, so every .bss byte trimmed is a heap byte gained. The TLS handshake on this
 * TF-M-constrained part runs the NS heap to the edge (mbedTLS 8K IN + 4K OUT records); the prior
 * all-ITS build had no LittleFS mounted and barely fit, so reclaiming this headroom is what lets
 * the cert-on-LFS build complete the handshake. 1536 still clears the ~1.2 KB key DER + margin. */
#define AWS_ENDPOINT_MAX   128
#define AWS_CERT_MAX       1536
#define AWS_KEY_MAX        1536

/* Fixed LittleFS path of the client-cert DER (kept OUTSIDE KV/ so env_init does not
 * slurp the cert bytes as an env value). Published as the env var "cert". */
#define AWS_CERT_PATH      "aws_cert.der"

/* Create the PSA-access mutex. Call once, pre-scheduler (single-threaded). */
void aws_provision_init(void);

/* BLE MAC as 12 lowercase hex chars + NUL (e.g. "112233aabbcc"). out must be >=13. */
int aws_get_ble_mac(char *out, size_t out_len);

/* Client certificate: base64 body -> DER -> the plain LittleFS file AWS_CERT_PATH,
 * then setenv("cert", AWS_CERT_PATH). The endpoint is stored directly with
 * setenv("endpoint", ...) at the call site (aws_cli.c); both are read back with
 * getenv() (auto-persisted to KV/ by the -Wl,--wrap=setenv hook). */
int aws_store_cert_file(const char *b64, size_t len);

/* Client private key: base64 body -> DER -> TF-M Internal Trusted Storage (secure). */
int aws_store_key(const char *b64, size_t len);
int aws_load_key(char *buf, size_t buf_len, size_t *out_len);    /* out_len = exact DER length */

#endif /* AWS_PROVISION_H */
