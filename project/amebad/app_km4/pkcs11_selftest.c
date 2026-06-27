/*
 * pkcs11_selftest.c — on-target corePKCS11 token self-test (AMEBAD_ENABLE_PKCS11).
 *
 * Exercises the full software token built on the upstream mbedTLS v3.6.4 backend
 * (component/amebad/mbedtls_port) with object persistence on LittleFS (the reused upstream POSIX
 * PAL over the AmebaD newlib VFS). Flow: PSA + threading init -> C_Initialize -> generate an
 * EC P-256 key pair (persisted via the PAL) -> sign a digest -> verify. Proves the mbedTLS
 * crypto path + the PAL storage end to end.
 */
#include <string.h>
#include "at_log.h"

#include "core_pkcs11.h"
#include "core_pkcs11_config.h"
#include "core_pkcs11_config_defaults.h"   /* pkcs11configLABEL_* (PAL-recognized object labels) */
#include "pkcs11.h"

#include "psa/crypto.h"
#include "ameba_soc.h"            /* DiagPrintf */

extern void mbedtls_threading_freertos_init( void );   /* component/amebad/mbedtls_port */

#define PK_OK( call )                                                       \
    do {                                                                    \
        CK_RV _rv = ( call );                                               \
        if( _rv != CKR_OK ) {                                               \
            log_error( "[PK11] FAIL %s -> 0x%x", #call, (unsigned int) _rv ); \
            return;                                                         \
        }                                                                   \
    } while( 0 )

/* DER of the named-curve OID for secp256r1 (P-256): 1.2.840.10045.3.1.7 */
static const CK_BYTE kP256Params[] = { 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07 };

void amebad_pkcs11_selftest( void )
{
    CK_FUNCTION_LIST_PTR pFn = NULL;
    CK_SESSION_HANDLE    xSession = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE     xPriv = CK_INVALID_HANDLE, xPub = CK_INVALID_HANDLE;
    CK_MECHANISM         xGen = { CKM_EC_KEY_PAIR_GEN, NULL, 0 };
    CK_MECHANISM         xSign = { CKM_ECDSA, NULL, 0 };
    CK_BBOOL             xTrue = CK_TRUE;
    CK_KEY_TYPE          xEc = CKK_EC;
    CK_BYTE              xDigest[ 32 ];
    CK_BYTE              xSig[ 64 ];
    CK_ULONG             xSigLen = sizeof( xSig );
    /* Use the well-known labels the bundled PAL maps to LittleFS files. */
    char                 privLabel[] = pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS;
    char                 pubLabel[]  = pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS;
    int                  i;

    CK_ATTRIBUTE pubTmpl[] = {
        { CKA_KEY_TYPE,    &xEc,        sizeof( xEc )        },
        { CKA_VERIFY,      &xTrue,      sizeof( xTrue )      },
        { CKA_EC_PARAMS,   ( void * ) kP256Params, sizeof( kP256Params ) },
        { CKA_LABEL,       pubLabel,    sizeof( pubLabel ) - 1 },
    };
    CK_ATTRIBUTE privTmpl[] = {
        { CKA_KEY_TYPE,    &xEc,        sizeof( xEc )        },
        { CKA_TOKEN,       &xTrue,      sizeof( xTrue )      },
        { CKA_PRIVATE,     &xTrue,      sizeof( xTrue )      },
        { CKA_SIGN,        &xTrue,      sizeof( xTrue )      },
        { CKA_LABEL,       privLabel,   sizeof( privLabel ) - 1 },
    };

    log_info( "[PK11] ===== corePKCS11 token self-test (mbedTLS backend) =====" );

    /* mbedTLS runtime: FreeRTOS threading callbacks + PSA. */
    mbedtls_threading_freertos_init();
    if( psa_crypto_init() != PSA_SUCCESS )
    {
        log_error( "[PK11] FAIL psa_crypto_init" );
        return;
    }

    PK_OK( C_GetFunctionList( &pFn ) );
    PK_OK( pFn->C_Initialize( NULL ) );
    PK_OK( pFn->C_OpenSession( 0, CKF_SERIAL_SESSION | CKF_RW_SESSION, NULL, NULL, &xSession ) );

    log_info( "[PK11] generating EC P-256 key pair..." );
    PK_OK( pFn->C_GenerateKeyPair( xSession, &xGen,
                                   pubTmpl,  sizeof( pubTmpl ) / sizeof( pubTmpl[ 0 ] ),
                                   privTmpl, sizeof( privTmpl ) / sizeof( privTmpl[ 0 ] ),
                                   &xPub, &xPriv ) );
    log_info( "[PK11] key pair OK (priv=%d pub=%d)",
                (int) xPriv, (int) xPub );

    for( i = 0; i < (int) sizeof( xDigest ); i++ )
    {
        xDigest[ i ] = ( CK_BYTE ) i;
    }
    PK_OK( pFn->C_SignInit( xSession, &xSign, xPriv ) );
    PK_OK( pFn->C_Sign( xSession, xDigest, sizeof( xDigest ), xSig, &xSigLen ) );
    log_info( "[PK11] sign OK (%d-byte signature)", (int) xSigLen );

    PK_OK( pFn->C_VerifyInit( xSession, &xSign, xPub ) );
    PK_OK( pFn->C_Verify( xSession, xDigest, sizeof( xDigest ), xSig, xSigLen ) );
    log_info( "[PK11] verify OK" );

    pFn->C_CloseSession( xSession );
    pFn->C_Finalize( NULL );
    log_info( "[PK11] ===== PASS (keys persisted to LittleFS) =====" );
}
