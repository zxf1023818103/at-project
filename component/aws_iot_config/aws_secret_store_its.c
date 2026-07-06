/*
 * aws_secret_store_its.c - AmebaD TF-M key backend: the client key DER goes to TrustZone-protected
 * Internal Trusted Storage (the only secret; the whole flash is RSIP-encrypted at rest). ITS has no
 * AEAD but stores the ~1.2 KB key DER as one object once ITS_MAX_ASSET_SIZE is raised (config.cmake).
 * Protected Storage (encrypted) is too constrained on this part (a psa_ps_set above ~256 B fails), so
 * ITS holds the key. Compiled + linked ONLY when AMEBAD_TFM_STORAGE (see the CMake); it overrides the
 * default aws_secret_store_file.c at link. Non-TF-M AmebaD + bl602 use the file backend instead.
 */
#include <stdint.h>
#include <string.h>

#include "psa/internal_trusted_storage.h"

#include "aws_iot.h"
#include "aws_secret_store.h"

/* TF-M ITS UID for the private key (arbitrary 64-bit label "AWS_KEY\1"). */
#define AWS_KEY_UID   ((psa_storage_uid_t)0x4157535F4B455901ULL)

/* Write + read-back verify, retrying on a content mismatch - belt-and-suspenders for a multi-page
 * object. NO DiagPrintf here: a LOG UART access right after a secure SPIC flash write takes an
 * imprecise BusFault (UART2 @ 0x48012000); the caller reports OK/FAIL via the CLI framework, deferred
 * past the transient. The read-back buffer is .bss (the SPM whitelists NS SRAM, and a ~2.5 KB stack
 * buffer would blow the CLI task); the DER passed in is already the .bss scratch in aws_provision.c. */
static int its_store(psa_storage_uid_t uid, const uint8_t *data, size_t len, size_t cap)
{
    static unsigned char vbuf[AWS_KEY_MAX];
    psa_status_t s;
    int          tries;

    if (data == NULL || len == 0 || len > cap || len > sizeof(vbuf)) return -1;
    for (tries = 0; tries < 3; tries++) {
        size_t       got = 0;
        psa_status_t g;
        s = psa_its_set(uid, len, data, PSA_STORAGE_FLAG_NONE);   /* stored without trailing NUL */
        if (s != PSA_SUCCESS) return -2;
        g = psa_its_get(uid, 0, len, vbuf, &got);
        if (g == PSA_SUCCESS && got == len && memcmp(vbuf, data, len) == 0) return 0;  /* verified */
        /* mismatch: the next psa_its_set overwrites; on final failure remove the corrupt object. */
    }
    (void) psa_its_remove(uid);
    return -3;
}

/* Reads the ITS object into buf, NUL-terminates, sets *out_len to bytes INCLUDING the NUL. */
static int its_load(psa_storage_uid_t uid, char *buf, size_t buf_len, size_t *out_len)
{
    struct psa_storage_info_t info;
    size_t                    got = 0;
    psa_status_t              s;

    if (buf == NULL || buf_len == 0) return -1;
    s = psa_its_get_info(uid, &info);
    if (s != PSA_SUCCESS) return -2;                 /* DOES_NOT_EXIST -> unprovisioned */
    if (info.size + 1 > buf_len) return -3;
    s = psa_its_get(uid, 0, buf_len - 1, buf, &got);
    if (s != PSA_SUCCESS) return -4;
    buf[got] = '\0';
    if (out_len != NULL) *out_len = got + 1;         /* include NUL for mbedTLS parsing */
    return 0;
}

int aws_secret_store_key(const uint8_t *der, size_t der_len)
{
    return its_store(AWS_KEY_UID, der, der_len, AWS_KEY_MAX);
}

int aws_secret_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    /* its_load counts the NUL it appended; the key is stored raw, so back out to the exact DER len. */
    int rc = its_load(AWS_KEY_UID, buf, buf_len, out_len);
    if (rc == 0 && out_len != NULL && *out_len > 0) (*out_len)--;
    return rc;
}
