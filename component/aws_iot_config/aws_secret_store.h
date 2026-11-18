/*
 * aws_secret_store.h - pluggable backend for the ONE secret AWS credential: the client private key.
 *
 * endpoint / cert / CA are never secret and always live in env + plain LittleFS DER files (handled in
 * aws_provision.c). Only the private key needs a per-platform secure home, so it goes through this
 * backend interface. Two implementations link-select:
 *   - aws_secret_store_file.c  (default): plain LittleFS DER file (AWS_KEY_PATH), path in env "key".
 *                                Used by bl602 (no secure world) and AmebaD non-TF-M builds.
 *   - aws_secret_store_its.c   (AmebaD TF-M): TrustZone-protected Internal Trusted Storage. Compiled
 *                                + linked only when AMEBAD_TFM_STORAGE, overriding the file impl.
 *
 * The store side receives the raw DER (aws_provision.c does the base64->DER decode into an NS-SRAM
 * .bss scratch, which the ITS write requires). The load side returns the raw DER (exact length in
 * *out_len); mbedtls_pk_parse_key auto-detects DER vs PEM.
 */
#ifndef AWS_SECRET_STORE_H
#define AWS_SECRET_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Persist the client key DER. 0 = ok, negative = error. */
int aws_secret_store_key(const uint8_t *der, size_t der_len);

/* Load the client key DER into buf (NUL-terminated for PEM autodetect); *out_len = exact DER bytes.
 * Returns 0 = ok, -2 = unprovisioned (no key stored), other negative = error. */
int aws_secret_load_key(char *buf, size_t buf_len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* AWS_SECRET_STORE_H */
