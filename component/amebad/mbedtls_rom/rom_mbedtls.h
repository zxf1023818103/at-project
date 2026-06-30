#ifndef AMEBAD_ROM_MBEDTLS_H
#define AMEBAD_ROM_MBEDTLS_H

/*
 * One-time setup to call the AmebaD A-cut ROM mbedTLS 2.4.0 as PURE SOFTWARE.
 *
 * Wires the ROM ram map (rom_ssl_ram_map @0x1000134c): calloc/free for the
 * alloc-using primitives (RSA / GCM / PK / cipher_setup) and, crucially, sets
 * use_hw_crypto_func = 0 so the ROM AES/GCM/DES routines take their software
 * branch and never touch the IPSEC HW engine (which hangs in this driver-only
 * image — see the AmebaD HW-crypto status note).
 *
 * Call once, early, before any mbedtls_* call. Idempotent. AES/SHA/MD/HMAC work
 * without allocation, but always call this so the HW gate is forced off.
 */
void rom_mbedtls_init(void);

#endif /* AMEBAD_ROM_MBEDTLS_H */
