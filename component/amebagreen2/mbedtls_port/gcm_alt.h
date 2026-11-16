/* Derived from mbedTLS 3.6.4 gcm.h, Apache-2.0. Keep the software state for multipart/non-96-bit fallback. */
#ifndef GREEN2_GCM_ALT_H
#define GREEN2_GCM_ALT_H


#if defined(MBEDTLS_GCM_LARGE_TABLE)
#define MBEDTLS_GCM_HTABLE_SIZE 256
#else
#define MBEDTLS_GCM_HTABLE_SIZE 16
#endif

/**
 * \brief          The GCM context structure.
 */
typedef struct mbedtls_gcm_context {
#if defined(MBEDTLS_BLOCK_CIPHER_C)
    mbedtls_block_cipher_context_t MBEDTLS_PRIVATE(block_cipher_ctx);  /*!< The cipher context used. */
#else
    mbedtls_cipher_context_t MBEDTLS_PRIVATE(cipher_ctx);    /*!< The cipher context used. */
#endif
    uint64_t MBEDTLS_PRIVATE(H)[MBEDTLS_GCM_HTABLE_SIZE][2]; /*!< Precalculated HTable. */
    uint64_t MBEDTLS_PRIVATE(len);                           /*!< The total length of the encrypted data. */
    uint64_t MBEDTLS_PRIVATE(add_len);                       /*!< The total length of the additional data. */
    unsigned char MBEDTLS_PRIVATE(base_ectr)[16];            /*!< The first ECTR for tag. */
    unsigned char MBEDTLS_PRIVATE(y)[16];                    /*!< The Y working value. */
    unsigned char MBEDTLS_PRIVATE(buf)[16];                  /*!< The buf working value. */
    unsigned char MBEDTLS_PRIVATE(mode);                     /*!< The operation to perform:
                                                              #MBEDTLS_GCM_ENCRYPT or
                                                              #MBEDTLS_GCM_DECRYPT. */
    unsigned char MBEDTLS_PRIVATE(acceleration);             /*!< The acceleration to use. */
    unsigned char hw_key[32];
    unsigned int hw_keybits;
}
mbedtls_gcm_context;


#endif
