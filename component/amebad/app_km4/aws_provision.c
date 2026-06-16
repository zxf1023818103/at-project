/*
 * aws_provision.c — credential storage for the AmebaD KM4 AWS IoT demo.
 *
 * All three credentials live in TF-M Internal Trusted Storage (ITS).
 *
 * NOTE on the storage choice:
 *  - LittleFS is not usable: the AWS image's larger .text (coreMQTT + mbedTLS) shifts the
 *    XIP-tail-pinned LittleFS .text onto the layout-sensitive address that hangs the KM4 at
 *    the first LittleFS mount (the QUIC-image XIP boot-hang class — doc/amebad_xip_layout_boot_hang.md).
 *  - Protected Storage (PS, encrypted) is too constrained on this part: a psa_ps_set above
 *    ~256 B (or the 3rd object) fails with GENERIC_ERROR — the CCM staging + the SFN
 *    shared-stack budget in the ~128 KB secure RAM.
 *  - ITS has no AEAD, so it stores each PEM (~1.2 KB cert / ~1.7 KB key) as one object once
 *    ITS_MAX_ASSET_SIZE is raised (config.cmake). ITS is TrustZone-protected from NS software
 *    and the whole flash is RSIP-encrypted at rest, so the key stays secure.
 *
 * Loaded PEM buffers are NUL-terminated and the returned length INCLUDES the NUL, as
 * mbedTLS PEM parsing (mbedtls_x509_crt_parse / mbedtls_pk_parse_key) requires.
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "psa/internal_trusted_storage.h"
#include "mbedtls/base64.h"
#include "aws_provision.h"

extern int DiagPrintf(const char *fmt, ...);

/* Extra PSA serialization on top of the TF-M NS interface. The NS dispatch is now serialized
 * (tfm_ns_interface_rtos.c), but this mutex is kept as belt-and-suspenders so the CLI task
 * (`set ...`) and the MQTT task (aws_is_provisioned / aws_load_*) never interleave a
 * multi-PSA-call sequence. Created pre-scheduler via aws_provision_init() (single-threaded then). */
static SemaphoreHandle_t s_ps_lock;

void aws_provision_init(void)
{
    if (s_ps_lock == NULL)
        s_ps_lock = xSemaphoreCreateMutex();
}

/* TEST (clean FS): s_ps_lock neutralized — last remaining difference vs the working pstest path. */
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

/* TF-M Internal Trusted Storage UIDs (arbitrary 64-bit, ASCII-ish labels). */
#define AWS_EP_UID       ((psa_storage_uid_t)0x4157535F45500001ULL)  /* "AWS_EP\0\1"  */
#define AWS_CERT_UID     ((psa_storage_uid_t)0x4157535F43525401ULL)  /* "AWS_CRT\1"   */
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

/* ----------------------------------------------------------- ITS helpers ---- */
static int its_store(psa_storage_uid_t uid, const char *data, size_t len, size_t cap)
{
    psa_status_t s;

    if (data == NULL || len == 0 || len > cap)
        return -1;
    ps_lock();
    s = psa_its_set(uid, len, data, PSA_STORAGE_FLAG_NONE);  /* stored without trailing NUL */
    ps_unlock();
    /* IMPORTANT: do NOT touch the LOG UART (DiagPrintf) here. Immediately after a secure SPIC
     * flash-program, a KM4 access to the KM0 LOG UART (UART2 @ 0x48012000) takes an imprecise
     * BusFault — the SPIC/clock domain has not settled. The caller reports status via the
     * FreeRTOS+CLI `out` buffer, which the framework prints later (after the transient clears),
     * exactly like the `pstest` command. */
    if (s != PSA_SUCCESS)
        return -2;
    return 0;
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

/* ----------------------------------------------------------- endpoint ------- */
int aws_store_endpoint(const char *endpoint)
{
    return its_store(AWS_EP_UID, endpoint,
                     endpoint ? strlen(endpoint) : 0, AWS_ENDPOINT_MAX);
}

int aws_load_endpoint(char *buf, size_t buf_len)
{
    return its_load(AWS_EP_UID, buf, buf_len, NULL);
}

/* ---- base64 body (armor + newlines stripped by the host) -> DER -> ITS ----
 * The console path passes the credential's bare base64 body as a CLI argument, so the NS-side
 * conversion is a plain mbedtls_base64_decode: the decoded bytes ARE the X.509 cert DER / the
 * PKCS#1|SEC1|PKCS#8 key DER (DER is ~30 % smaller than PEM, and mbedtls_{x509_crt,pk}_parse
 * auto-detect DER vs PEM on load). The DER lands in a static (.bss) buffer, NOT the heap: the
 * TF-M SPM rejects a PS/ITS write whose source buffer is outside the NS SRAM it whitelists
 * (e.g. a PSRAM/heap buffer), so the ITS write must marshal from .bss. */
static int b64_store(psa_storage_uid_t uid, const char *b64, size_t len, size_t cap, const char *what)
{
    static unsigned char der[AWS_KEY_MAX];   /* AWS_KEY_MAX == AWS_CERT_MAX (2560) */
    size_t der_len = 0;
    int r, rc;

    if (b64 == NULL || len == 0)
        return -1;
    r = mbedtls_base64_decode(der, sizeof(der), &der_len, (const unsigned char *)b64, len);
    if (r != 0)
        return -1;   /* decode error; caller reports via the CLI out buffer */
    (void)what;
    /* No DiagPrintf after its_store(): a LOG UART access right after the secure flash write
     * faults (see its_store). The caller prints the OK/FAIL verdict via the CLI framework. */
    rc = its_store(uid, (const char *)der, der_len, cap);
    return rc;
}

/* DER is stored without a trailing NUL; its_load appends + counts one (for PEM parsing), so the
 * exact DER length is *out_len - 1. mbedtls_{pk,x509_crt}_parse take the buffer + exact length. */
static int der_load(psa_storage_uid_t uid, char *buf, size_t buf_len, size_t *out_len)
{
    int rc = its_load(uid, buf, buf_len, out_len);
    if (rc == 0 && out_len != NULL && *out_len > 0)
        (*out_len)--;
    return rc;
}

/* ----------------------------------------------------------- client cert ---- */
int aws_store_cert(const char *b64, size_t len)
{
    return b64_store(AWS_CERT_UID, b64, len, AWS_CERT_MAX, "cert");
}

int aws_load_cert(char *buf, size_t buf_len, size_t *out_len)
{
    return der_load(AWS_CERT_UID, buf, buf_len, out_len);
}

/* ----------------------------------------------------------- private key ---- */
int aws_store_key(const char *b64, size_t len)
{
    return b64_store(AWS_KEY_UID, b64, len, AWS_KEY_MAX, "key");
}

int aws_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    return der_load(AWS_KEY_UID, buf, buf_len, out_len);
}

/* ----------------------------------------------------------- provisioned? --- */
int aws_is_provisioned(void)
{
    struct psa_storage_info_t info;
    int ok;

    /* Only the private key lives in secure storage; cert + endpoint are embedded. */
    ps_lock();
    ok = (psa_its_get_info(AWS_KEY_UID, &info) == PSA_SUCCESS);
    ps_unlock();
    return ok;
}
