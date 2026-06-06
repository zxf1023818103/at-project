/*
 * rom_hwcrypto_test.c — comparative test of the crypto functions behind
 * component/amebad/mbedtls_rom/rom_ssl_ram_map.h.
 *
 * That ram-map's hw_crypto_aes_* / hw_crypto_des_* pointers are the AmebaD
 * IPSEC hardware-crypto-engine routines (rtl_crypto_aes_*), selected by the ROM
 * mbedTLS when use_hw_crypto_func=1. Our image runs use_hw_crypto_func=0 (the
 * pure-software path). This test puts the two side by side on one AES-128-ECB
 * vector (FIPS-197 C.1):
 *   SW path: the ROM mbedTLS software AES (what the image uses)  -> known good
 *   HW path: CRYPTO_Init + rtl_crypto_aes_ecb_init/encrypt (the IPSEC engine)
 *
 * The HW routines are mask-ROM at fixed addresses (Thumb-tagged / odd); called
 * directly here since app_km4's symbol table intentionally omits them. Addresses
 * from component/amebad/bootloader_km4/rlx8721d_rom_symbol_acut_boot.ld.
 *
 * Prior finding (reference_amebad_hw_crypto): CRYPTO_Init returns 0 (pure
 * register writes), but the first AES *process* call stalls the AHB forever
 * (missing secure-boot bus bring-up). A print precedes every HW call so the
 * hang point is exact; if the HW path returns, its output is compared to SW.
 */
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "rom_mbedtls_crypto.h"   /* ROM mbedTLS software AES */

extern void DiagPrintf(const char *, ...);

/* IPSEC HW-crypto ROM routines = what rom_ssl_ram_map.hw_crypto_aes_ecb_* point to. */
typedef int (*crypto_init_fn)(void *);
typedef int (*aes_init_fn)(const unsigned char *key, unsigned int keylen);
typedef int (*aes_ecb_fn)(const unsigned char *msg, unsigned int msglen,
                          const unsigned char *iv, unsigned int ivlen,
                          unsigned char *result);
#define ROM_CRYPTO_Init     ((crypto_init_fn)0x10104a49u)
#define ROM_aes_ecb_init    ((aes_init_fn)0x10103d19u)
#define ROM_aes_ecb_encrypt ((aes_ecb_fn)0x10103d69u)

static void hex16(const char *tag, const unsigned char *b)
{
    int i;
    DiagPrintf("%s", tag);
    for (i = 0; i < 16; i++) DiagPrintf("%02x", b[i]);
    DiagPrintf("\n\r");
}

void rom_hwcrypto_compare(void)
{
    /* FIPS-197 Appendix C.1 AES-128 ECB vector. */
    static const unsigned char key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    static const unsigned char pt[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    static const unsigned char ct[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
        0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
    unsigned char sw[16] = {0}, hw[16] = {0};
    int r;

    DiagPrintf("\n\r[HWCMP] ===== ROM crypto HW vs SW (rom_ssl_ram_map hw_crypto_*) =====\n\r");

    /* ---- SW path: ROM mbedTLS software AES (use_hw_crypto_func=0) ---- */
    {
        mbedtls_aes_context c;
        mbedtls_aes_init(&c);
        mbedtls_aes_setkey_enc(&c, key, 128);
        mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, pt, sw);
        mbedtls_aes_free(&c);
        hex16("[HWCMP] SW  AES-128-ECB = ", sw);
        DiagPrintf("[HWCMP] SW  vs FIPS-197  = %s\n\r",
                   memcmp(sw, ct, 16) == 0 ? "MATCH" : "MISMATCH");
    }

    /* ---- HW path: the IPSEC engine (rom_ssl_ram_map.hw_crypto_aes_ecb_*) ---- */
    DiagPrintf("[HWCMP] --- HW IPSEC engine (rtl_crypto_aes_ecb_*) ---\n\r");
    DiagPrintf("[HWCMP] CRYPTO_Init(NULL) ...\n\r");
    r = ROM_CRYPTO_Init(0);
    DiagPrintf("[HWCMP]   CRYPTO_Init = %d\n\r", r);

    DiagPrintf("[HWCMP] rtl_crypto_aes_ecb_init(key,16) ...\n\r");
    r = ROM_aes_ecb_init(key, 16);
    DiagPrintf("[HWCMP]   aes_ecb_init = %d\n\r", r);

    DiagPrintf("[HWCMP] rtl_crypto_aes_ecb_encrypt()  <-- prior finding: AHB stall HANGS here\n\r");
    r = ROM_aes_ecb_encrypt(pt, 16, 0, 0, hw);   /* no output after this line if it hangs */

    DiagPrintf("[HWCMP]   aes_ecb_encrypt = %d  (HW path SURVIVED)\n\r", r);
    hex16("[HWCMP] HW  AES-128-ECB = ", hw);
    DiagPrintf("[HWCMP] HW  vs SW       = %s\n\r", memcmp(hw, sw, 16) == 0 ? "MATCH" : "MISMATCH");
    DiagPrintf("[HWCMP] ===== done =====\n\r");
}
