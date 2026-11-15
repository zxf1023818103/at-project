/* Dplus ROM AES-GCM DMA with cache-safe staging and the upstream software
 * implementation for multipart operations and arbitrary IV lengths. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "dplus_gcm_sw.h"
#include <string.h>
#include <stdlib.h>

void mbedtls_gcm_init(mbedtls_gcm_context *c) { dplus_sw_gcm_init(c); }
void mbedtls_gcm_free(mbedtls_gcm_context *c) { dplus_sw_gcm_free(c); }
int mbedtls_gcm_setkey(mbedtls_gcm_context *c, mbedtls_cipher_id_t cipher,
                       const unsigned char *key, unsigned bits)
{
    c->hw_keybits = 0;
    int rc = dplus_sw_gcm_setkey(c, cipher, key, bits);
    if (!rc && cipher == MBEDTLS_CIPHER_ID_AES) {
        memcpy(c->hw_key, key, bits/8); c->hw_keybits = bits;
    }
    return rc;
}

int mbedtls_gcm_starts(mbedtls_gcm_context *c, int mode, const unsigned char *iv, size_t n)
{ return dplus_sw_gcm_starts(c, mode, iv, n); }
int mbedtls_gcm_update_ad(mbedtls_gcm_context *c, const unsigned char *a, size_t n)
{ return dplus_sw_gcm_update_ad(c, a, n); }
int mbedtls_gcm_update(mbedtls_gcm_context *c, const unsigned char *in, size_t n,
                       unsigned char *out, size_t size, size_t *written)
{ return dplus_sw_gcm_update(c, in, n, out, size, written); }
int mbedtls_gcm_finish(mbedtls_gcm_context *c, unsigned char *out, size_t size,
                       size_t *written, unsigned char *tag, size_t taglen)
{ return dplus_sw_gcm_finish(c, out, size, written, tag, taglen); }

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *c, int mode, size_t n,
    const unsigned char *iv, size_t ivlen, const unsigned char *aad, size_t alen,
    const unsigned char *in, unsigned char *out, size_t tlen, unsigned char *tag)
{
    if ((mode != MBEDTLS_GCM_ENCRYPT && mode != MBEDTLS_GCM_DECRYPT) || tlen < 4 || tlen > 16)
        return MBEDTLS_ERR_GCM_BAD_INPUT;
    /* DMA lengths are bounded; software remains valid for large/nonstandard calls. */
    if (!c->hw_keybits || ivlen != 12 || n > 16384 || alen > 1024)
        return dplus_sw_gcm_crypt_and_tag(c, mode, n, iv, ivlen, aad, alen, in, out, tlen, tag);
    size_t msgsz = (n + 31) & ~(size_t)31, adsz = (alen + 31) & ~(size_t)31;
    size_t total = 128 + 2 * msgsz + adsz;
    unsigned char *allocation = malloc(total + 31);
    if (!allocation)
        return dplus_sw_gcm_crypt_and_tag(c, mode, n, iv, ivlen, aad, alen, in, out, tlen, tag);
    unsigned char *key = (void *)(((uintptr_t)allocation + 31) & ~(uintptr_t)31);
    unsigned char *nonce = key+32, *fulltag = key+64, *input = key+128;
    unsigned char *output = input+msgsz, *ad = output+msgsz;
    memset(key, 0, total);
    memcpy(key, c->hw_key, c->hw_keybits/8); memcpy(nonce, iv, 12);
    if (n) memcpy(input, in, n);
    if (alen) memcpy(ad, aad, alen);
    /* IPC semaphore serializes KM0 and KM4 as well as the local Wi-Fi crypto users. */
    TickType_t start = xTaskGetTickCount();
    while (!IPC_SEMTake(IPC_SEM_CRYPTO, 0xfff)) {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(2000)) {
            mbedtls_platform_zeroize(key, total); free(allocation);
            return MBEDTLS_ERR_GCM_BAD_INPUT;
        }
        vTaskDelay(1);
    }
    int rc = CRYPTO_Init(NULL);
    CRYPTO_OTPKey_Init(0, DISABLE);
    if (!rc) rc = rtl_crypto_aes_gcm_init(key, c->hw_keybits/8);
    DCache_CleanInvalidate((u32)key, total);
    if (!rc) rc = mode == MBEDTLS_GCM_ENCRYPT
        ? rtl_crypto_aes_gcm_encrypt(input, n, nonce, ad, alen, output, fulltag)
        : rtl_crypto_aes_gcm_decrypt(input, n, nonce, ad, alen, output, fulltag);
    DCache_Invalidate((u32)key, total);
    IPC_SEMFree(IPC_SEM_CRYPTO);
    if (!rc) { if (n) memcpy(out, output, n); memcpy(tag, fulltag, tlen); }
    mbedtls_platform_zeroize(key, total); free(allocation);
    return rc ? MBEDTLS_ERR_GCM_BAD_INPUT : 0;
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *c, size_t n,
    const unsigned char *iv, size_t ivlen, const unsigned char *aad, size_t alen,
    const unsigned char *tag, size_t tlen, const unsigned char *in, unsigned char *out)
{
    unsigned char actual[16], diff = 0;
    int rc = mbedtls_gcm_crypt_and_tag(c, MBEDTLS_GCM_DECRYPT, n, iv, ivlen,
                                      aad, alen, in, out, tlen, actual);
    if (rc) return rc;
    for (size_t i = 0; i < tlen; ++i) diff |= actual[i] ^ tag[i];
    mbedtls_platform_zeroize(actual, sizeof(actual));
    if (diff) { mbedtls_platform_zeroize(out, n); return MBEDTLS_ERR_GCM_AUTH_FAILED; }
    return 0;
}
