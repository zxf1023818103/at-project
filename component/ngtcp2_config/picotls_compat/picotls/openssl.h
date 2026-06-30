/*
 * picotls/openssl.h — compat shim so ngtcp2's picotls crypto binding
 * (crypto/picotls/picotls.c, which #includes <picotls/openssl.h>) builds against picotls's
 * mbedTLS/PSA backend instead of OpenSSL (unavailable on this bare-metal target).
 *
 * The binding only takes the ADDRESSES of a fixed set of picotls algorithm objects + calls
 * ptls_openssl_random_bytes; every one has an exact ptls_mbedtls_* twin (same picotls types),
 * so aliasing the names redirects the binding onto the mbedTLS backend with no source change.
 * This shim is on ngtcp2_crypto_picotls's include path AHEAD of picotls's real openssl.h.
 */
#ifndef AT_NGTCP2_PICOTLS_OPENSSL_COMPAT_H
#define AT_NGTCP2_PICOTLS_OPENSSL_COMPAT_H

#include <picotls/mbedtls.h>

#define ptls_openssl_aes128gcm          ptls_mbedtls_aes128gcm
#define ptls_openssl_aes256gcm          ptls_mbedtls_aes256gcm
#define ptls_openssl_chacha20poly1305   ptls_mbedtls_chacha20poly1305
#define ptls_openssl_aes128ecb          ptls_mbedtls_aes128ecb
#define ptls_openssl_aes256ecb          ptls_mbedtls_aes256ecb
#define ptls_openssl_chacha20           ptls_mbedtls_chacha20
#define ptls_openssl_sha256             ptls_mbedtls_sha256
#define ptls_openssl_random_bytes       ptls_mbedtls_random_bytes

#endif /* AT_NGTCP2_PICOTLS_OPENSSL_COMPAT_H */
