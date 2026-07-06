/*
 * aws_provision.c - shared AWS IoT credential provisioning (both AmebaD and Bouffalo).
 *
 * Credentials split by secrecy:
 *   - endpoint    -> env "endpoint" (set directly in aws_cli.c; --wrap=setenv persists it to KV).
 *   - client cert -> plain LittleFS DER file AWS_CERT_PATH, path published as env "cert".
 *   - server CA   -> plain LittleFS DER file AWS_CA_PATH,   path published as env "ca".
 *   - private key -> the pluggable secret-store backend (aws_secret_store.h): a plain LittleFS file
 *                    (bl602 / AmebaD non-TF-M) or TF-M Internal Trusted Storage (AmebaD TF-M).
 *
 * The console passes each credential's bare base64 body as one CLI argument; this decodes it to DER
 * (the decoded bytes ARE the X.509 cert DER / PKCS#1|SEC1|PKCS#8 key DER; mbedtls_{x509_crt,pk}_parse
 * auto-detect DER vs PEM on load). The key DER lands in the .bss scratch s_der, NOT the heap: the
 * TF-M SPM rejects an ITS write whose source is outside the NS SRAM it whitelists, so the ITS backend
 * needs the DER in .bss. cert/CA reuse the same scratch (the `aws` sub-verbs never run concurrently).
 */
#include <stdint.h>
#include <stdio.h>          /* fopen / fwrite / fclose (cert + CA -> plain LittleFS files) */
#include <stdlib.h>         /* setenv (publish cert/CA paths as env vars) */
#include <string.h>

#include "mbedtls/base64.h"

#include "aws_iot.h"
#include "aws_secret_store.h"

/* Single .bss scratch, reused for cert / CA / key (the base64 DER never overlaps across sub-verbs).
 * .bss (not heap) is mandatory for the ITS key backend's NS-SRAM marshaling. */
static unsigned char s_der[AWS_KEY_MAX];

/* base64 body -> DER -> plain LittleFS file at `path`, then publish `path` as env `env_name`. */
static int prv_b64_to_file(const char *b64, size_t len, const char *path, const char *env_name)
{
    size_t der_len = 0, wrote;
    FILE  *f;
    int    r;

    if (b64 == NULL || len == 0) return -1;
    r = mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *) b64, len);
    if (r != 0 || der_len == 0) return -1;                 /* decode error / too big */

    f = fopen(path, "w");
    if (f == NULL) return -2;
    wrote = fwrite(s_der, 1, der_len, f);
    fclose(f);
    if (wrote != der_len) return -3;

    if (setenv(env_name, path, 1) != 0) return -4;          /* auto-persisted to KV by --wrap=setenv */
    return 0;
}

int aws_store_cert(const char *b64, size_t len)
{
    return prv_b64_to_file(b64, len, AWS_CERT_PATH, "cert");
}

int aws_store_ca(const char *b64, size_t len)
{
    return prv_b64_to_file(b64, len, AWS_CA_PATH, "ca");
}

/* Key: decode base64 -> DER in .bss, then hand the DER to the secret-store backend (file or ITS). */
int aws_store_key(const char *b64, size_t len)
{
    size_t der_len = 0;
    int    r;

    if (b64 == NULL || len == 0) return -1;
    r = mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *) b64, len);
    if (r != 0 || der_len == 0) return -1;
    return aws_secret_store_key(s_der, der_len);
}

int aws_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    return aws_secret_load_key(buf, buf_len, out_len);
}

/* Optional pre-scheduler init hook (AmebaD's TF-M build overrides this to create a PSA mutex). Weak
 * no-op default so bl602 / non-TF-M can call it harmlessly. */
__attribute__((weak)) void aws_provision_init(void) { }
