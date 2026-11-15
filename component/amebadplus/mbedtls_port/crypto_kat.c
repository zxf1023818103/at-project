/* NIST AES-GCM / SHA-256 known-answer tests, including the ROM DMA path. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mbedtls/gcm.h"
#include <stdio.h>
#include <string.h>

static void extended_gcm_kat(void)
{
    /* Independent OpenSSL AESGCM results: key=00..17/1f, IV=00..0b,
     * AAD=00..12, plaintext=00..24. Exercises partial blocks and AAD. */
    static const unsigned char expected[2][53] = {
        {0xe6,0xf8,0x20,0x98,0x9d,0xbc,0xcf,0x09,0xd8,0x3a,0xd6,0x89,0xf3,0xa4,0xd2,0x7f,
         0x1e,0x8e,0x21,0x18,0x2c,0xb4,0x40,0xa9,0x67,0x46,0x71,0x23,0xd1,0x8b,0x3f,0x43,
         0xf7,0x55,0xd5,0x52,0x3e,0xe7,0x8b,0x3d,0x96,0x13,0xc6,0x63,0xb0,0x73,0x19,0xb6,
         0xa2,0x51,0x20,0x3f,0x8b},
        {0x47,0x03,0xd4,0x18,0xc1,0xe0,0xc4,0x1c,0x85,0x48,0x9d,0x80,0xbd,0xe4,0x76,0x62,
         0x93,0xc7,0x95,0x27,0xe4,0x6e,0x49,0x6b,0x20,0x7e,0xff,0x9e,0x01,0x74,0x1e,0xad,
         0x21,0x31,0x8c,0xdf,0x8b,0x3b,0x44,0x87,0x77,0x3e,0x2d,0xe8,0xd3,0x3b,0x8b,0xdf,
         0xc1,0xbe,0x46,0x91,0x6e}
    };
    unsigned char key[32], iv[12], aad[19], plain[37], out[37], tag[16];
    for (unsigned i=0;i<sizeof(key);++i) key[i]=i;
    for (unsigned i=0;i<sizeof(iv);++i) iv[i]=i;
    for (unsigned i=0;i<sizeof(aad);++i) aad[i]=i;
    for (unsigned i=0;i<sizeof(plain);++i) plain[i]=i;
    for (unsigned v=0;v<2;++v) {
        mbedtls_gcm_context ctx;
        mbedtls_gcm_init(&ctx);
        configASSERT(!mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, v ? 256 : 192));
        configASSERT(!mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, sizeof(plain),
            iv, sizeof(iv), aad, sizeof(aad), plain, out, sizeof(tag), tag));
        configASSERT(!memcmp(out,expected[v],sizeof(out)) && !memcmp(tag,expected[v]+sizeof(out),sizeof(tag)));
        configASSERT(!mbedtls_gcm_auth_decrypt(&ctx,sizeof(out),iv,sizeof(iv),aad,sizeof(aad),
            tag,12,out,out)); /* truncated tag and in-place decryption */
        configASSERT(!memcmp(out,plain,sizeof(out)));
        mbedtls_gcm_free(&ctx);
    }
    printf("[GCM-KAT] AES-192/256, AAD, partial blocks, truncated tag, in-place PASS\n");
}

void amebadplus_crypto_kat(void)
{
    static const unsigned char ciphertext[16] = {
        0x03,0x88,0xda,0xce,0x60,0xb6,0xa3,0x92,0xf3,0x28,0xc2,0xb9,0x71,0xb2,0xfe,0x78};
    static const unsigned char expected_tag[16] = {
        0xab,0x6e,0x47,0xd4,0x2c,0xec,0x13,0xbd,0xf5,0x3a,0x67,0xb2,0x12,0x57,0xbd,0xdf};
    unsigned char key[32] ALIGNMTO(32) = {0}, iv[32] ALIGNMTO(32) = {0};
    unsigned char plain[32] ALIGNMTO(32) = {0}, output[32] ALIGNMTO(32) = {0};
    unsigned char tag[32] ALIGNMTO(32) = {0};
    while (!IPC_SEMTake(IPC_SEM_CRYPTO, 0xfff)) vTaskDelay(1);
    int rc = CRYPTO_Init(NULL);
    CRYPTO_OTPKey_Init(0, DISABLE);
    if (!rc) rc = rtl_crypto_aes_gcm_init(key, 16);
    if (!rc) rc = rtl_crypto_aes_gcm_encrypt(plain, 16, iv, NULL, 0, output, tag);
    IPC_SEMFree(IPC_SEM_CRYPTO);
    configASSERT(!rc && !memcmp(output, ciphertext, 16) && !memcmp(tag, expected_tag, 16));
    printf("[ROM-KAT] Dplus AES-128-GCM encrypt/tag PASS\n");
    static const unsigned char sha256_abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    unsigned char abc[32] ALIGNMTO(32) = {'a','b','c'};
    while (!IPC_SEMTake(IPC_SEM_CRYPTO, 0xfff)) vTaskDelay(1);
    rc = CRYPTO_SHA_Init(NULL);
    DCache_CleanInvalidate((u32)abc,sizeof(abc));
    DCache_CleanInvalidate((u32)output,sizeof(output));
    if (!rc) rc = rtl_crypto_sha2(SHA2_256,abc,3,output);
    DCache_Invalidate((u32)output,sizeof(output));
    IPC_SEMFree(IPC_SEM_CRYPTO);
    configASSERT(!rc && !memcmp(output,sha256_abc,sizeof(sha256_abc)));
    printf("[ROM-KAT] Dplus SHA-256 PASS\n");
    mbedtls_gcm_context c;
    mbedtls_gcm_init(&c);
    configASSERT(mbedtls_gcm_setkey(&c, MBEDTLS_CIPHER_ID_AES, key, 128) == 0);
    /* Deliberately unaligned caller buffers exercise the ALT's cache staging. */
    configASSERT(mbedtls_gcm_crypt_and_tag(&c, MBEDTLS_GCM_ENCRYPT, 16, iv, 12,
        NULL, 0, plain+1, output+1, 16, tag+1) == 0);
    configASSERT(!memcmp(output+1, ciphertext, 16) && !memcmp(tag+1, expected_tag, 16));
    configASSERT(mbedtls_gcm_auth_decrypt(&c, 16, iv, 12, NULL, 0,
        tag+1, 16, output+1, plain+1) == 0);
    tag[1] ^= 1;
    memset(plain+1, 0xaa, 16);
    configASSERT(mbedtls_gcm_auth_decrypt(&c, 16, iv, 12, NULL, 0,
        tag+1, 16, output+1, plain+1) == MBEDTLS_ERR_GCM_AUTH_FAILED);
    configASSERT(!memcmp(plain+1, key, 16));
    size_t n = 0, total = 0;
    configASSERT(!mbedtls_gcm_starts(&c, MBEDTLS_GCM_ENCRYPT, iv, 12));
    configASSERT(!mbedtls_gcm_update(&c, plain, 7, output, 32, &n)); total += n;
    configASSERT(!mbedtls_gcm_update(&c, plain+7, 9, output+total, 32-total, &n)); total += n;
    configASSERT(!mbedtls_gcm_finish(&c, output+total, 32-total, &n, tag, 16)); total += n;
    configASSERT(total == 16 && !memcmp(output, ciphertext, 16) && !memcmp(tag, expected_tag, 16));
    mbedtls_gcm_free(&c);
    printf("[GCM-KAT] unaligned encrypt/decrypt, bad-tag zeroization, multipart PASS\n");
    extended_gcm_kat();
}
