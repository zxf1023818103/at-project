/*
 * aws_secret_store_file.c - default (no-secure-world) key backend: the client key DER lives in a
 * plain LittleFS file (AWS_KEY_PATH), its path published as env "key". NS-readable but, on platforms
 * with flash encryption at rest (bl602 RSIP-off has none; AmebaD non-TF-M is RSIP-encrypted), that is
 * the best available. Used by bl602 and AmebaD non-TF-M builds. AmebaD TF-M builds link
 * aws_secret_store_its.c instead (see AMEBAD_TFM_STORAGE in the CMake).
 */
#include <stdio.h>
#include <stdlib.h>       /* setenv / getenv */
#include <string.h>

#include "aws_iot.h"
#include "aws_secret_store.h"

int aws_secret_store_key(const uint8_t *der, size_t der_len)
{
    FILE  *f;
    size_t wrote;

    if (der == NULL || der_len == 0) return -1;
    f = fopen(AWS_KEY_PATH, "w");
    if (f == NULL) return -2;
    wrote = fwrite(der, 1, der_len, f);
    fclose(f);
    if (wrote != der_len) return -3;
    /* publish the key path as env "key" (auto-persisted to KV/key by --wrap=setenv), mirroring
     * the cert's env "cert"; aws_secret_load_key reads it back. */
    if (setenv("key", AWS_KEY_PATH, 1) != 0) return -4;
    return 0;
}

int aws_secret_load_key(char *buf, size_t buf_len, size_t *out_len)
{
    const char *path = getenv("key");
    FILE  *f;
    size_t got;

    if (buf == NULL || buf_len == 0) return -1;
    if (path == NULL || path[0] == '\0') path = AWS_KEY_PATH;   /* fall back to the fixed path */
    f = fopen(path, "r");
    if (f == NULL) return -2;                                    /* no file -> unprovisioned */
    got = fread(buf, 1, buf_len - 1, f);                         /* leave room to NUL-terminate */
    fclose(f);
    if (got == 0) return -3;
    buf[got] = '\0';
    if (out_len != NULL) *out_len = got;                         /* exact DER length */
    return 0;
}
