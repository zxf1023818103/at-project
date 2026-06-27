/*
 * aws_provision.c - credential storage for the AmebaD KM4 AWS IoT demo.
 *
 * Credentials are split by secrecy:
 *  - endpoint    -> env var "endpoint" (set directly with setenv in aws_cli.c; the
 *                   -Wl,--wrap=setenv hook persists it to plain LittleFS KV/endpoint).
 *  - client cert -> plain LittleFS DER file AWS_CERT_PATH ("aws_cert.der"); its path is
 *                   published as env var "cert". aws_store_cert_file() lives here.
 *  - private key -> TF-M Internal Trusted Storage (the only secret), handled below.
 *
 * Why ITS for the key (only): ITS is TrustZone-protected from NS software and the whole
 * flash is RSIP-encrypted at rest, so the key stays secure. It has no AEAD but stores the
 * ~1.7 KB key DER as one object once ITS_MAX_ASSET_SIZE is raised (config.cmake).
 * Protected Storage (encrypted) is too constrained on this part - a psa_ps_set above
 * ~256 B fails with GENERIC_ERROR (CCM staging + the SFN shared-stack budget in ~128 KB
 * secure RAM) - so ITS (not PS) holds the key, and the cert moved to plain LittleFS
 * (verified to mount without the historical XIP boot-hang in this image's layout).
 *
 * The loaded key buffer is the raw DER (exact length in *out_len); mbedtls_pk_parse_key
 * auto-detects DER vs PEM.
 */
#include <stdint.h>
#include <stdio.h>          /* fopen / fwrite / fclose (cert -> plain LittleFS file) */
#include <stdlib.h>         /* setenv (publish the cert path as env var "cert") */
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#ifdef AMEBAD_TFM_STORAGE
#include "psa/internal_trusted_storage.h"   /* key -> TrustZone-protected ITS (TF-M build) */
#endif
#include "mbedtls/base64.h"
#include "aws_provision.h"


/* Extra PSA serialization on top of the TF-M NS interface. The NS dispatch is now serialized
 * (tfm_ns_interface_rtos.c), but this mutex is kept as belt-and-suspenders so the CLI task
 * (`set key ...`) and the MQTT task (aws_load_key) never interleave a multi-PSA-call sequence.
 * Created pre-scheduler via aws_provision_init() (single-threaded then). */
static SemaphoreHandle_t s_ps_lock;

void aws_provision_init(void)
{
    if (s_ps_lock == NULL)
        s_ps_lock = xSemaphoreCreateMutex();
}

/* TEST (clean FS): s_ps_lock neutralized - last remaining difference vs the working pstest path. */
static void ps_lock(void)   { (void)s_ps_lock; }
static void ps_unlock(void) { }

/* efuse logical read (component/amebad/mbed/efuse_api.c, added to the AWS build) +
 * the osdep device lock guarding the efuse map. Declared here to avoid pulling the
 * mbed/osdep header trees into this TU. */
extern int  efuse_logical_read(uint16_t addr, uint16_t size, uint8_t *pbuf);
extern void device_mutex_lock(uint32_t device);
extern void device_mutex_unlock(uint32_t device);
#define DEV_LOCK_EFUSE   0u           /* RT_DEV_LOCK_EFUSE (osdep/device_lock.h) */
#define EFUSE_BLE_MAC    0x190        /* logical efuse address of the 6-byte BT/BLE MAC */

/* TF-M Internal Trusted Storage UID for the private key (arbitrary 64-bit label).
 * Endpoint + cert no longer use ITS (they live in plain LittleFS). */
#define AWS_KEY_UID      ((psa_storage_uid_t)0x4157535F4B455901ULL)  /* "AWS_KEY\1"   */

/* ---------------------------------------------------------------- BLE MAC ---- */
int aws_get_ble_mac(char *out, size_t out_len)
{
    uint8_t mac[6];
    int ret;

    if (out == NULL || out_len < 13)
        return -1;

    device_mutex_lock(DEV_LOCK_EFUSE);
    ret = efuse_logical_read(EFUSE_BLE_MAC, sizeof(mac), mac);
    device_mutex_unlock(DEV_LOCK_EFUSE);
    if (ret != 0)
        return -2;

    for (int i = 0; i < 6; i++) {
        static const char hex[] = "0123456789abcdef";
        out[i * 2]     = hex[(mac[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[mac[i] & 0xf];
    }
    out[12] = '\0';
    return 0;
}

/* ----------------------------------------------------------- ITS helpers ---- *
 * (TF-M build only; the non-TF-M build stores the key in a plain LittleFS file instead - see the
 * #else aws_store_key/aws_load_key below.) */
#ifdef AMEBAD_TFM_STORAGE
static int its_store(psa_storage_uid_t uid, const char *data, size_t len, size_t cap)
{
    /* Read-back verify buffer in .bss (not heap/stack - SPM whitelists NS SRAM, and a 2.5 KB
     * stack buffer would blow the CLI task). */
    static unsigned char vbuf[AWS_KEY_MAX];
    psa_status_t s;
    int try;

    if (data == NULL || len == 0 || len > cap || len > sizeof(vbuf))
        return -1;

    /* Write + read-back verify, retrying on a content mismatch - belt-and-suspenders for a large
     * multi-page object (a ~1.2 KB cert / DER key). No DiagPrintf here: a LOG UART access right
     * after a secure SPIC flash write takes an imprecise BusFault (UART2 @ 0x48012000); the caller
     * reports the OK/FAIL verdict via the FreeRTOS+CLI `out` buffer, deferred past the transient. */
    for (try = 0; try < 3; try++) {
        size_t got = 0;
        psa_status_t g;
        ps_lock();
        s = psa_its_set(uid, len, data, PSA_STORAGE_FLAG_NONE);   /* stored without trailing NUL */
        if (s != PSA_SUCCESS) {
            ps_unlock();
            return -2;
        }
        g = psa_its_get(uid, 0, len, vbuf, &got);                /* read-back (memory-mapped) */
        ps_unlock();
        if (g == PSA_SUCCESS && got == len && memcmp(vbuf, data, len) == 0)
            return 0;                                            /* verified good */
        /* mismatch: the next psa_its_set overwrites; on final failure remove so we don't leave a
         * corrupt object behind (the loader then sees "unprovisioned" and idle-retries). */
    }
    ps_lock();
    (void)psa_its_remove(uid);
    ps_unlock();
    return -3;   /* persistent verify failure */
}

/* Reads the ITS object into buf, NUL-terminates, sets *out_len to bytes INCLUDING NUL. */
static int its_load(psa_storage_uid_t uid, char *buf, size_t buf_len, size_t *out_len)
{
    struct psa_storage_info_t info;
    size_t got = 0;
    psa_status_t s;

    if (buf == NULL || buf_len == 0)
        return -1;
    ps_lock();
    s = psa_its_get_info(uid, &info);
    if (s != PSA_SUCCESS) {
        ps_unlock();
        return -2;                                  /* DOES_NOT_EXIST -> unprovisioned */
    }
    if (info.size + 1 > buf_len) {
        ps_unlock();
        return -3;
    }
    s = psa_its_get(uid, 0, buf_len - 1, buf, &got);
    ps_unlock();
    if (s != PSA_SUCCESS)
        return -4;
    buf[got] = '\0';
    if (out_len != NULL)
        *out_len = got + 1;                         /* include NUL for mbedTLS parsing */
    return 0;
}
#endif /* AMEBAD_TFM_STORAGE (ITS helpers) */

/* ---- base64 body (armor + newlines stripped by the host) -> DER --------------
 * The console path passes the credential's bare base64 body as a CLI argument, so the NS-side
 * conversion is a plain mbedtls_base64_decode: the decoded bytes ARE the X.509 cert DER / the
 * PKCS#1|SEC1|PKCS#8 key DER (DER is ~30 % smaller than PEM, and mbedtls_{x509_crt,pk}_parse
 * auto-detect DER vs PEM on load). The key DER lands in this static (.bss) buffer, NOT the heap:
 * the TF-M SPM rejects an ITS write whose source buffer is outside the NS SRAM it whitelists
 * (e.g. a PSRAM/heap buffer), so the ITS write must marshal from .bss. The cert path reuses the
 * same buffer (the two `set` commands never run concurrently). */
static unsigned char s_der[AWS_KEY_MAX];   /* AWS_KEY_MAX == AWS_CERT_MAX (2560); cert (always) + key */

#ifdef AMEBAD_TFM_STORAGE
static int b64_store(psa_storage_uid_t uid, const char *b64, size_t len, size_t cap)
{
    size_t der_len = 0;
    int r, rc;

    if (b64 == NULL || len == 0)
        return -1;
    r = mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *)b64, len);
    if (r != 0)
        return -1;   /* decode error; caller reports via the CLI out buffer */
    /* No DiagPrintf after its_store(): a LOG UART access right after the secure flash write
     * faults (see its_store). The caller prints the OK/FAIL verdict via the CLI framework. */
    rc = its_store(uid, (const char *)s_der, der_len, cap);
    return rc;
}

/* DER is stored without a trailing NUL; its_load appends + counts one, so the exact DER length is
 * *out_len - 1. mbedtls_pk_parse_key takes the buffer + exact length. */
static int der_load(psa_storage_uid_t uid, char *buf, size_t buf_len, size_t *out_len)
{
    int rc = its_load(uid, buf, buf_len, out_len);
    if (rc == 0 && out_len != NULL && *out_len > 0)
        (*out_len)--;
    return rc;
}
#endif /* AMEBAD_TFM_STORAGE (b64_store / der_load) */

/* ----------------------------------------------------------- client cert ---- *
 * Not secret -> plain LittleFS. base64 body -> DER -> the file AWS_CERT_PATH, then publish the
 * path as env var "cert" (the TLS transport loads it with mbedtls_x509_crt_parse_file). The DER
 * write goes through stdio (fopen -> the newlib VFS -> LittleFS); the NS-SRAM marshaling
 * constraint does NOT apply here (it is only for psa_its_set across the secure SPE boundary). */
int aws_store_cert_file(const char *b64, size_t len)
{
    size_t der_len = 0, wrote;
    FILE  *f;
    int    r;

    if (b64 == NULL || len == 0)
        return -1;
    r = mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *)b64, len);
    if (r != 0 || der_len == 0)
        return -1;
    f = fopen(AWS_CERT_PATH, "w");
    if (f == NULL)
        return -2;
    wrote = fwrite(s_der, 1, der_len, f);
    fclose(f);
    if (wrote != der_len)
        return -3;
    /* publish the cert path as env var "cert" (auto-persisted to KV/cert by --wrap=setenv) */
    if (setenv("cert", AWS_CERT_PATH, 1) != 0)
        return -4;
    return 0;
}

/* ----------------------------------------------------------- private key ---- *
 * TF-M build: the key is the one secret that goes to TrustZone-protected ITS. Non-TF-M build: there
 * is no secure storage, so the key falls back to a plain LittleFS DER file (AWS_KEY_PATH) - the same
 * treatment as the cert. The file is NS-readable but, like the cert, RSIP-encrypted on flash at rest;
 * this is the best available without the secure world. The DER write goes through stdio -> the VFS ->
 * LittleFS (no NS-SRAM marshaling constraint, that is only for psa_its_set across the SPE boundary). */
#ifdef AMEBAD_TFM_STORAGE
int aws_store_key(const char *b64, size_t len)
{
    return b64_store(AWS_KEY_UID, b64, len, AWS_KEY_MAX);
}

int aws_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    return der_load(AWS_KEY_UID, buf, buf_len, out_len);
}
#else  /* TF-M off: private key -> plain LittleFS file */
int aws_store_key(const char *b64, size_t len)
{
    size_t der_len = 0, wrote;
    FILE  *f;
    int    r;

    if (b64 == NULL || len == 0)
        return -1;
    r = mbedtls_base64_decode(s_der, sizeof(s_der), &der_len, (const unsigned char *)b64, len);
    if (r != 0 || der_len == 0)
        return -1;
    f = fopen(AWS_KEY_PATH, "w");
    if (f == NULL)
        return -2;
    wrote = fwrite(s_der, 1, der_len, f);
    fclose(f);
    if (wrote != der_len)
        return -3;
    /* publish the key path as env var "key" (auto-persisted to KV/key by --wrap=setenv), exactly
     * mirroring the cert's env "cert"; aws_load_key reads it back. */
    if (setenv("key", AWS_KEY_PATH, 1) != 0)
        return -4;
    return 0;
}

int aws_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    const char *path = getenv("key");
    FILE  *f;
    size_t got;

    if (buf == NULL || buf_len == 0)
        return -1;
    if (path == NULL || path[0] == '\0')
        path = AWS_KEY_PATH;                 /* fall back to the fixed path if env "key" is unset */
    f = fopen(path, "r");
    if (f == NULL)
        return -2;                          /* no file -> unprovisioned */
    got = fread(buf, 1, buf_len - 1, f);    /* leave room to NUL-terminate (mbedtls PEM autodetect) */
    fclose(f);
    if (got == 0)
        return -3;
    buf[got] = '\0';
    if (out_len != NULL)
        *out_len = got;                     /* exact DER length */
    return 0;
}
#endif /* AMEBAD_TFM_STORAGE */
