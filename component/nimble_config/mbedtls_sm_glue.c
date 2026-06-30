/*
 * mbedtls_sm_glue.c — back NimBLE 1.9.0's Security Manager crypto with upstream Mbed TLS
 * (component/amebad/mbedtls_port, v3.6.4) instead of wolfCrypt. The wolfSSL replacement
 * (see wolfcrypt_sm_glue.c, which this supersedes — same approach, mbedTLS backend).
 *
 * NimBLE 1.9.0's ble_sm_alg.c hard-codes the tinycrypt API, so we re-implement the small
 * primitive surface it (and BLE Mesh) calls in terms of Mbed TLS:
 *
 *   AES-128 single block   tc_aes128_set_encrypt_key / tc_aes_encrypt
 *                          -> mbedtls_aes_setkey_enc / mbedtls_aes_crypt_ecb (ECB).
 *                          tinycrypt's cmac_mode.c is KEPT and rides on these, so
 *                          AES-CMAC (f4/f5/f6/g2) flows through Mbed TLS too.
 *   ECDH P-256             uECC_make_key / uECC_shared_secret / uECC_valid_public_key
 *                          / uECC_set_rng -> mbedtls_ecp_* / mbedtls_mpi_*.
 *
 * The matching upstream sources are dropped from the build (component/amebad/nimble_port
 * REMOVE_ITEM): ext/tinycrypt/src/{aes_encrypt,ecc,ecc_dh,ecc_dsa,ecc_platform_specific}.c.
 * cmac_mode.c / utils.c / aes_decrypt.c stay.
 *
 * Byte order: micro-ecc (this tinycrypt build) works in BIG-ENDIAN byte arrays — public =
 * X(32)||Y(32), private = d(32), secret = X(32). ble_sm_alg swaps to/from BLE little-endian
 * around the calls; mbedtls_mpi_read/write_binary is big-endian, so the formats line up 1:1.
 *
 * RNG: the ECDH PRIVATE KEY is drawn from NimBLE's RNG (uECC_set_rng -> the controller HCI
 * LE_Rand, crypto-strong). Mbed TLS's f_rng (sm_blind_rng -> mbedtls_hardware_poll) is used
 * ONLY for mbedtls_ecp_mul point blinding, never for key material.
 *
 * Threading: ensure_init() registers the Mbed TLS FreeRTOS threading-alt callbacks
 * (mbedtls_threading_freertos_init) before the first crypto op, since MBEDTLS_THREADING_ALT is on.
 */
#include <string.h>
#include <stdio.h>

#include "mbedtls/aes.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"

#include "tinycrypt/aes.h"        /* struct tc_aes_key_sched_struct, TCAesKeySched_t */
#include "tinycrypt/constants.h"  /* TC_CRYPTO_SUCCESS / TC_CRYPTO_FAIL */
#include "tinycrypt/ecc.h"        /* uECC_Curve, uECC_RNG_Function, uECC_word_t */
#include "tinycrypt/ecc_dh.h"     /* uECC_make_key / uECC_shared_secret prototypes */

/* 1 = full Mbed TLS SM (AES/CMAC + ECDH P-256), tinycrypt ecc*.c dropped (shipped).
 * 0 = AES/CMAC on Mbed TLS only, ECDH stays on tinycrypt. */
#define SM_ECC_MBEDTLS 1

extern void mbedtls_threading_freertos_init( void );                                   /* component/amebad/mbedtls_port */
extern int  mbedtls_hardware_poll( void *, unsigned char *, size_t, size_t * ); /* entropy alt (port) */

static int s_inited;
static void ensure_init( void )
{
    if( !s_inited )
    {
        mbedtls_threading_freertos_init();   /* MBEDTLS_THREADING_ALT callbacks */
        s_inited = 1;
    }
}

/* f_rng for ecp_mul point blinding only (NOT key material). */
static int sm_blind_rng( void *ctx, unsigned char *out, size_t len )
{
    size_t olen;
    ( void ) ctx;
    return ( mbedtls_hardware_poll( (void *) 0, out, len, &olen ) == 0 ) ? 0 : -1;
}

/* ---------------------------------------------------------------- AES (ECB) -- */

int
tc_aes128_set_encrypt_key( TCAesKeySched_t s, const uint8_t *k )
{
    if( s == (TCAesKeySched_t) 0 || k == (const uint8_t *) 0 )
    {
        return TC_CRYPTO_FAIL;
    }
    /* Stash the raw 16-byte key in the schedule storage; expand per block below
     * (SM/CMAC process only a handful of blocks). */
    memcpy( s->words, k, 16 );
    return TC_CRYPTO_SUCCESS;
}

int
tc_aes_encrypt( uint8_t *out, const uint8_t *in, const TCAesKeySched_t s )
{
    mbedtls_aes_context aes;
    int rc;

    if( out == (uint8_t *) 0 || in == (const uint8_t *) 0 || s == (TCAesKeySched_t) 0 )
    {
        return TC_CRYPTO_FAIL;
    }
    ensure_init();
    mbedtls_aes_init( &aes );
    rc = mbedtls_aes_setkey_enc( &aes, (const unsigned char *) s->words, 128 );
    if( rc == 0 )
    {
        rc = mbedtls_aes_crypt_ecb( &aes, MBEDTLS_AES_ENCRYPT, in, out );
    }
    mbedtls_aes_free( &aes );
    return ( rc == 0 ) ? TC_CRYPTO_SUCCESS : TC_CRYPTO_FAIL;
}

/* ------------------------------------------------------------- ECDH (P-256) -- */
#if SM_ECC_MBEDTLS

static uECC_RNG_Function g_uecc_rng;

void
uECC_set_rng( uECC_RNG_Function rng_function )
{
    g_uecc_rng = rng_function;
}

uECC_RNG_Function
uECC_get_rng( void )
{
    return g_uecc_rng;
}

/* pub: 64B (X||Y, big-endian), priv: 32B (big-endian). */
int
uECC_make_key( uint8_t *p_public_key, uint8_t *p_private_key, uECC_Curve curve )
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi d;
    int tries, ok = TC_CRYPTO_FAIL;

    ( void ) curve;
    if( p_public_key == (uint8_t *) 0 || p_private_key == (uint8_t *) 0 ||
        g_uecc_rng == (uECC_RNG_Function) 0 )
    {
        return TC_CRYPTO_FAIL;
    }
    ensure_init();
    mbedtls_ecp_group_init( &grp );
    mbedtls_ecp_point_init( &Q );
    mbedtls_mpi_init( &d );

    if( mbedtls_ecp_group_load( &grp, MBEDTLS_ECP_DP_SECP256R1 ) == 0 )
    {
        for( tries = 0; tries < 16; tries++ )
        {
            if( g_uecc_rng( p_private_key, 32 ) != 1 )
            {
                break;
            }
            if( mbedtls_mpi_read_binary( &d, p_private_key, 32 ) != 0 )
            {
                continue;
            }
            /* d must be in [1, n-1] (mbedtls_ecp_mul also checks, but skip early). */
            if( mbedtls_mpi_cmp_int( &d, 1 ) < 0 || mbedtls_mpi_cmp_mpi( &d, &grp.N ) >= 0 )
            {
                continue;
            }
            if( mbedtls_ecp_mul( &grp, &Q, &d, &grp.G, sm_blind_rng, (void *) 0 ) != 0 )
            {
                continue;
            }
            /* Q is normalized to affine; export X(32)||Y(32) big-endian. */
            if( mbedtls_mpi_write_binary( &Q.MBEDTLS_PRIVATE( X ), p_public_key, 32 ) == 0 &&
                mbedtls_mpi_write_binary( &Q.MBEDTLS_PRIVATE( Y ), p_public_key + 32, 32 ) == 0 )
            {
                ok = TC_CRYPTO_SUCCESS;
                break;
            }
        }
    }

    mbedtls_mpi_free( &d );
    mbedtls_ecp_point_free( &Q );
    mbedtls_ecp_group_free( &grp );
    return ok;
}

/* Import a 64-byte (X||Y) big-endian point into |Q| as affine (Z=1). */
static int
import_pub( mbedtls_ecp_point *Q, const uint8_t *p )
{
    if( mbedtls_mpi_read_binary( &Q->MBEDTLS_PRIVATE( X ), p, 32 ) != 0 ||
        mbedtls_mpi_read_binary( &Q->MBEDTLS_PRIVATE( Y ), p + 32, 32 ) != 0 ||
        mbedtls_mpi_lset( &Q->MBEDTLS_PRIVATE( Z ), 1 ) != 0 )
    {
        return -1;
    }
    return 0;
}

/* secret: 32B = X coordinate of d*Q (big-endian). */
int
uECC_shared_secret( const uint8_t *p_public_key, const uint8_t *p_private_key,
                    uint8_t *p_secret, uECC_Curve curve )
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Qp, R;
    mbedtls_mpi d;
    int ok = TC_CRYPTO_FAIL;

    ( void ) curve;
    if( p_public_key == (const uint8_t *) 0 || p_private_key == (const uint8_t *) 0 ||
        p_secret == (uint8_t *) 0 )
    {
        return TC_CRYPTO_FAIL;
    }
    ensure_init();
    mbedtls_ecp_group_init( &grp );
    mbedtls_ecp_point_init( &Qp );
    mbedtls_ecp_point_init( &R );
    mbedtls_mpi_init( &d );

    if( mbedtls_ecp_group_load( &grp, MBEDTLS_ECP_DP_SECP256R1 ) == 0 &&
        import_pub( &Qp, p_public_key ) == 0 &&
        mbedtls_ecp_check_pubkey( &grp, &Qp ) == 0 &&
        mbedtls_mpi_read_binary( &d, p_private_key, 32 ) == 0 &&
        mbedtls_ecp_mul( &grp, &R, &d, &Qp, sm_blind_rng, (void *) 0 ) == 0 &&
        mbedtls_mpi_write_binary( &R.MBEDTLS_PRIVATE( X ), p_secret, 32 ) == 0 )
    {
        ok = TC_CRYPTO_SUCCESS;
    }

    mbedtls_mpi_free( &d );
    mbedtls_ecp_point_free( &R );
    mbedtls_ecp_point_free( &Qp );
    mbedtls_ecp_group_free( &grp );
    return ok;
}

/* >= 0 if the 64-byte (X||Y) point is a valid P-256 public key, < 0 otherwise. */
int
uECC_valid_public_key( const uint8_t *public_key, uECC_Curve curve )
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    int rc;

    ( void ) curve;
    if( public_key == (const uint8_t *) 0 )
    {
        return -1;
    }
    ensure_init();
    mbedtls_ecp_group_init( &grp );
    mbedtls_ecp_point_init( &Q );

    rc = -1;
    if( mbedtls_ecp_group_load( &grp, MBEDTLS_ECP_DP_SECP256R1 ) == 0 &&
        import_pub( &Q, public_key ) == 0 &&
        mbedtls_ecp_check_pubkey( &grp, &Q ) == 0 )   /* on-curve, not infinity, correct order */
    {
        rc = 0;
    }

    mbedtls_ecp_point_free( &Q );
    mbedtls_ecp_group_free( &grp );
    return rc;
}

/* --- curve_secp256r1's function-pointer fields (tinycrypt/ecc.h static const). Never
 * invoked here (we bypass the curve struct), present only so the linker resolves the
 * &double_jacobian_default / &x_side_default / &vli_mmod_fast_secp256r1 referenced by
 * every TU that includes ecc.h. */
void double_jacobian_default( uECC_word_t *X1, uECC_word_t *Y1, uECC_word_t *Z1, uECC_Curve curve )
{
    ( void ) X1; ( void ) Y1; ( void ) Z1; ( void ) curve;
}
void x_side_default( uECC_word_t *result, const uECC_word_t *x, uECC_Curve curve )
{
    ( void ) result; ( void ) x; ( void ) curve;
}
void vli_mmod_fast_secp256r1( unsigned int *result, unsigned int *product )
{
    ( void ) result; ( void ) product;
}
#endif /* SM_ECC_MBEDTLS */

/* ------------------------------------------------------------------- KAT --- */
/* amebad_nimble_sm_crypto_selftest() — prove the Mbed TLS-backed primitives are correct
 * before pairing. Same vectors as the wolfCrypt glue (AES-128-ECB FIPS-197, AES-CMAC
 * RFC 4493, ECDH P-256 symmetry + BT-spec DHKey + pubkey-validate). Called from
 * amebad_nimble_start(). */
#include "tinycrypt/cmac_mode.h"

static const uint8_t kPrivA[32] = {
    0x3f,0x49,0xf6,0xd4,0xa3,0xc5,0x5f,0x38,0x74,0xc9,0xb3,0xe3,0xd2,0x10,0x3f,0x50,
    0x4a,0xff,0x60,0x7b,0xeb,0x40,0xb7,0x99,0x58,0x99,0xb8,0xa6,0xcd,0x3c,0x1a,0xbd };
static const uint8_t kPubA[64] = {
    0x20,0xb0,0x03,0xd2,0xf2,0x97,0xbe,0x2c,0x5e,0x2c,0x83,0xa7,0xe9,0xf9,0xa5,0xb9,
    0xef,0xf4,0x91,0x11,0xac,0xf4,0xfd,0xdb,0xcc,0x03,0x01,0x48,0x0e,0x35,0x9d,0xe6,
    0xdc,0x80,0x9c,0x49,0x65,0x2a,0xeb,0x6d,0x63,0x32,0x9a,0xbf,0x5a,0x52,0x15,0x5c,
    0x76,0x63,0x45,0xc2,0x8f,0xed,0x30,0x24,0x74,0x1c,0x8e,0xd0,0x15,0x89,0xd2,0x8b };
static const uint8_t kPrivB[32] = {
    0x55,0x18,0x8b,0x3d,0x32,0xf6,0xbb,0x9a,0x90,0x0a,0xfc,0xfb,0xee,0xd4,0xe7,0x2a,
    0x59,0xcb,0x9a,0xc2,0xf1,0x9d,0x7c,0xfb,0x6b,0x4f,0xdd,0x49,0xf4,0x7f,0xc5,0xfd };
static const uint8_t kPubB[64] = {
    0x1e,0xa1,0xf0,0xf0,0x1f,0xaf,0x1d,0x96,0x09,0x59,0x22,0x84,0xf1,0x9e,0x4c,0x00,
    0x47,0xb5,0x8a,0xfd,0x86,0x15,0xa6,0x9f,0x55,0x90,0x77,0xb2,0x2f,0xaa,0xa1,0x90,
    0x4c,0x55,0xf3,0x3e,0x42,0x9d,0xad,0x37,0x73,0x56,0x70,0x3a,0x9a,0xb8,0x51,0x60,
    0x47,0x2d,0x11,0x30,0xe2,0x8e,0x36,0x76,0x5f,0x89,0xaf,0xf9,0x15,0xb1,0x21,0x4a };
static const uint8_t kDHKey[32] = {
    0xec,0x02,0x34,0xa3,0x57,0xc8,0xad,0x05,0x34,0x10,0x10,0xa6,0x0a,0x39,0x7d,0x9b,
    0x99,0x79,0x6b,0x13,0xb4,0xf8,0x66,0xf1,0x86,0x8d,0x34,0xf3,0x73,0xbf,0xa6,0x98 };

static int kat_rng( uint8_t *dst, unsigned int size )
{
    size_t olen;
    return mbedtls_hardware_poll( (void *) 0, dst, size, &olen ) == 0 ? 1 : 0;
}
static int eq( const uint8_t *a, const uint8_t *b, int n ) { return memcmp( a, b, n ) == 0; }

void
amebad_nimble_sm_crypto_selftest( void )
{
    int pass = 1;

    /* ---- AES-128-ECB (FIPS-197) ---- */
    {
        static const uint8_t key[16] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
        static const uint8_t pt[16]  = {
            0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
        static const uint8_t ct[16]  = {
            0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a };
        struct tc_aes_key_sched_struct s;
        uint8_t out[16];
        int ok = tc_aes128_set_encrypt_key( &s, key ) == TC_CRYPTO_SUCCESS &&
                 tc_aes_encrypt( out, pt, &s ) == TC_CRYPTO_SUCCESS && eq( out, ct, 16 );
        printf( "[SM-KAT] AES-128-ECB (mbedTLS): %s\n\r", ok ? "PASS" : "FAIL" );
        pass &= ok;
    }

    /* ---- AES-CMAC (RFC 4493), rides on tc_aes_encrypt ---- */
    {
        static const uint8_t key[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
        static const uint8_t m16[16] = {
            0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a };
        static const uint8_t t0[16]  = {
            0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46 };
        static const uint8_t t16[16] = {
            0x07,0x0a,0x16,0xb4,0x6b,0x4d,0x41,0x44,0xf7,0x9b,0xdd,0x9d,0xd0,0x4a,0x28,0x7c };
        struct tc_aes_key_sched_struct sched;
        struct tc_cmac_struct state;
        uint8_t tag[16];
        int ok = 1;
        ok &= tc_cmac_setup( &state, key, &sched ) != TC_CRYPTO_FAIL &&
              tc_cmac_update( &state, (const uint8_t *) 0, 0 ) != TC_CRYPTO_FAIL &&
              tc_cmac_final( tag, &state ) != TC_CRYPTO_FAIL && eq( tag, t0, 16 );
        ok &= tc_cmac_setup( &state, key, &sched ) != TC_CRYPTO_FAIL &&
              tc_cmac_update( &state, m16, 16 ) != TC_CRYPTO_FAIL &&
              tc_cmac_final( tag, &state ) != TC_CRYPTO_FAIL && eq( tag, t16, 16 );
        printf( "[SM-KAT] AES-CMAC (RFC4493): %s\n\r", ok ? "PASS" : "FAIL" );
        pass &= ok;
    }

    /* ---- ECDH P-256 ---- */
#if SM_ECC_MBEDTLS
    {
        uECC_RNG_Function saved = uECC_get_rng();
        uint8_t pubC[64], privC[32], s1[32], s2[32];
        int ok_sym, ok_spec, ok_valid;

        uECC_set_rng( kat_rng );          /* KAT randomness for the ephemeral key */

        ok_sym = uECC_make_key( pubC, privC, (uECC_Curve) 0 ) == TC_CRYPTO_SUCCESS &&
                 uECC_shared_secret( kPubA, privC, s1, (uECC_Curve) 0 ) == TC_CRYPTO_SUCCESS &&
                 uECC_shared_secret( pubC, kPrivA, s2, (uECC_Curve) 0 ) == TC_CRYPTO_SUCCESS &&
                 eq( s1, s2, 32 );
        printf( "[SM-KAT] ECDH P-256 symmetry: %s\n\r", ok_sym ? "PASS" : "FAIL" );

        ok_spec = uECC_shared_secret( kPubB, kPrivA, s1, (uECC_Curve) 0 ) == TC_CRYPTO_SUCCESS &&
                  eq( s1, kDHKey, 32 ) &&
                  uECC_shared_secret( kPubA, kPrivB, s2, (uECC_Curve) 0 ) == TC_CRYPTO_SUCCESS &&
                  eq( s2, kDHKey, 32 );
        printf( "[SM-KAT] ECDH P-256 spec DHKey: %s\n\r", ok_spec ? "PASS" : "FAIL" );

        {
            uint8_t bad[64];
            memcpy( bad, kPubA, 64 );
            bad[63] ^= 0x01;
            ok_valid = uECC_valid_public_key( kPubA, (uECC_Curve) 0 ) >= 0 &&
                       uECC_valid_public_key( bad, (uECC_Curve) 0 ) < 0;
        }
        printf( "[SM-KAT] P-256 pubkey validate: %s\n\r", ok_valid ? "PASS" : "FAIL" );

        uECC_set_rng( saved );
        pass &= ok_sym & ok_spec & ok_valid;
    }
#endif /* SM_ECC_MBEDTLS */

    printf( "[SM-KAT] mbedTLS SM crypto: %s\n\r", pass ? "ALL PASS" : "FAILURES ABOVE" );
}
