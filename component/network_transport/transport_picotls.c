/*
 * @file transport_picotls.c
 * @brief TLS transport interface over picotls (TLS 1.3) + FreeRTOS+TCP sockets.
 *
 * A drop-in alternative to transport_mbedtls.c, exporting the same TLS_FreeRTOS_* API so
 * coreMQTT/coreHTTP and the application are unchanged; the backend is chosen at link time
 * (see transport_tls.h / the TLS_BACKEND build flag).
 *
 * picotls is the handshake + record engine (TLS 1.3 only). It is socket-agnostic — it
 * consumes/produces buffers — so this file pumps those buffers over the shared TCP_Sockets_*
 * layer directly (no mbedTLS BIO). mbedTLS is used only for: (a) crypto primitives, through
 * picotls's ptls_mbedtls backend (ptls_mbedtls_cipher_suites/key_exchanges/random_bytes);
 * (b) X.509 server-certificate verification (ptls's mbedtls backend ships none — we port the
 * openssl backend's verify_certificate/verify_sign to mbedtls_x509, see picotls/lib/openssl.c);
 * (c) client-key signing for mutual TLS (a software mbedtls_pk sign_certificate callback —
 * picotls's ptls_mbedtls_load_private_key only takes a PEM filename + signs PKCS1v15-raw via PSA,
 * neither of which fits the in-memory-DER, TLS1.3-PSS client key).
 *
 * One TLS connection at a time (the AWS demo reconnects sequentially): the ptls_context_t,
 * the verifier, the signer and the parsed cert chains are file-scope static.
 *
 * Define PICOTLS_SKIP_CERT_VERIFY for a bring-up build that does NOT verify the server
 * certificate (like the QUIC smoke test). INSECURE — trusted-LAN debugging only.
 */

#include "logging_levels.h"

#ifndef LIBRARY_LOG_NAME
    #define LIBRARY_LOG_NAME    "PicotlsTransport"
#endif
#ifndef LIBRARY_LOG_LEVEL
    #define LIBRARY_LOG_LEVEL    LOG_INFO
#endif

#include <stdio.h>
/* Route the transport's Log* through printf -> the timestamped console (the light
 * logging_stack.h leaves SdkLog to the consumer). */
#ifndef SdkLog
    #define SdkLog( message )    printf message
#endif

#include "logging_stack.h"

/* Standard includes. */
#include <string.h>
#include <stdint.h>

/* FreeRTOS includes (xTaskGetTickCount / pdMS_TO_TICKS for the handshake timeout). */
#include "FreeRTOS.h"
#include "task.h"

/* mbedTLS: PSA crypto init + X.509 verify + PK sign/verify for the cert/key glue. */
#include "psa/crypto.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"

/* picotls core + mbedTLS crypto backend. */
#include "picotls.h"
#include "picotls/mbedtls.h"

/* TCP Sockets Wrapper (TCP_Sockets_*) + the picotls transport surface. */
#include "tcp_sockets_wrapper.h"
#include "transport_picotls.h"

#include <sys/stat.h>   /* stat / S_ISDIR -- folder vs single-file root CA path */

#ifdef AT_PROJECT_CRT_BUNDLE
    #include "esp_crt_bundle.h"
#endif

/* Optional heap tracing of the TLS handshake (compile with -DAT_PROJECT_TLS_HEAP_TRACE). Logs free heap
 * (and min-ever-free, meaningful only when CONFIG_MM_ENABLE_MIN_FREE_TRACKING=1) at the connect /
 * handshake / disconnect milestones, so the transient handshake PEAK (the Certificate flight) reads
 * off as the low point of the `free` column. No-op by default. */
#if defined( AT_PROJECT_TLS_HEAP_TRACE )
    #define TLS_HEAP_MARK( tag )                                                  \
    LogInfo( ( "[HEAPTRACE] %-24s free=%u minever=%u", ( tag ),                    \
               ( unsigned ) xPortGetFreeHeapSize(),                               \
               ( unsigned ) xPortGetMinimumEverFreeHeapSize() ) )
#else
    #define TLS_HEAP_MARK( tag )    ( ( void ) 0 )
#endif

/*-----------------------------------------------------------*/

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

/* Size of this backend's params block, for the fallback dispatcher (transport_fallback.c). */
size_t TLS_FreeRTOS_params_size( void )
{
    return sizeof( TlsTransportParams_t );
}

/*-----------------------------------------------------------*/
/* TLS handshake wall-clock cap (matches the mbedTLS transport). */
#define PICOTLS_HANDSHAKE_TIMEOUT_MS    ( 10000U )

/* Ciphertext read chunk for the handshake / record loops. picotls reassembles records
 * larger than this internally, so an MTU-ish chunk is plenty (and keeps the stack small). */
#define PICOTLS_RX_CIPHER_CHUNK         ( 2048U )

/* Max size of one TLS 1.3 record on the wire = 5-byte header + 2^14 plaintext + 256 AEAD/content-type
 * overhead. TLS_FreeRTOS_recv frames whole records into a buffer of this size so ptls_receive gets a
 * complete record and takes picotls' zero-alloc fast path (no per-record recvbuf.rec churn). */
#define PICOTLS_MAX_RECORD              ( 5U + ( 1U << 14 ) + 256U )

/* rxReserveSize at/above which a connection opts into record-framing (a resident PICOTLS_MAX_RECORD
 * buffer). Only bulk-download callers (ota_http passes 16640) clear this; small-record connections
 * (MQTT/telemetry) stay on the chunked path so heap-tight parts aren't forced to spare 16 KB. */
#define PICOTLS_FRAME_MIN_RESERVE       ( 8192U )

/*-----------------------------------------------------------*/
/* One-time picotls context (crypto backend wiring; credentials are set per-connection). */
static ptls_context_t s_ptlsCtx;
static int            s_ctxReady;

/* picotls needs a clock for ticket/cert freshness. We verify the chain without date checks
 * (MBEDTLS_HAVE_TIME is off) and never resume, so a monotonic ms counter suffices — same as
 * the QUIC path (quic_test.c). */
static uint64_t picotls_now( ptls_get_time_t * self )
{
    ( void ) self;
    return ( uint64_t ) xTaskGetTickCount();
}
static ptls_get_time_t s_ptlsGetTime = { picotls_now };

/* ---- HW-AEAD compatibility shim ---------------------------------------------------------------
 * picotls's aead_encrypt_v (lib/mbedtls.c) drives the PSA *multi-part* AEAD: one psa_aead_update
 * per input segment. The RTL8721D gcm_alt / chachapoly_alt HW engines are one-shot (a single
 * update only — the IPSEC ROM exposes only one-shot CipherEncryptAD, not gcm_update/final), so a
 * TLS record's 2-segment input (payload + the TLS 1.3 content-type byte) fails the 2nd update with
 * PSA_ERROR_NOT_SUPPORTED. We wrap the AEAD so the encrypt path COALESCES the input vector into one
 * buffer and re-enters picotls's own encrypt_v with incnt==1 -- exactly one update -> the HW
 * one-shot path. (Decrypt is already one-shot in picotls: psa_aead_decrypt.) Only built when the HW
 * engine is compiled in (MBEDTLS_GCM_ALT); software builds keep the native multi-part path (no
 * coalesce copy). The picotls submodule stays pristine -- only public st_ptls_aead_context_t /
 * algorithm / cipher-suite fields are touched. */
#if defined( MBEDTLS_GCM_ALT )

typedef void ( * aead_encrypt_v_fn )( struct st_ptls_aead_context_t *, void *, ptls_iovec_t *, size_t,
                                      uint64_t, const void *, size_t );
typedef int  ( * aead_setup_fn )( ptls_aead_context_t *, int, const void *, const void * );

static aead_encrypt_v_fn s_origEncryptV;   /* picotls's aead_encrypt_v (same for every AEAD algo) */

static void aead_coalesce_encrypt_v( struct st_ptls_aead_context_t * ctx, void * output,
                                     ptls_iovec_t * input, size_t incnt, uint64_t seq,
                                     const void * aad, size_t aadlen )
{
    uint8_t stackbuf[ 2048 ];   /* TLS records are tiny here (MQTT publishes / handshake flights) */
    uint8_t * buf = stackbuf;
    ptls_iovec_t one;
    size_t total = 0, off = 0, i;

    if( incnt <= 1 )   /* already single-segment -> one update -> the HW one-shot path is happy */
    {
        s_origEncryptV( ctx, output, input, incnt, seq, aad, aadlen );
        return;
    }

    for( i = 0; i < incnt; i++ )
    {
        total += input[ i ].len;
    }
    if( total > sizeof( stackbuf ) )
    {
        buf = pvPortMalloc( total );
        if( buf == NULL )   /* OOM: fall back to the native path (fails on HW, but never crashes) */
        {
            s_origEncryptV( ctx, output, input, incnt, seq, aad, aadlen );
            return;
        }
    }
    for( i = 0; i < incnt; i++ )
    {
        memcpy( buf + off, input[ i ].base, input[ i ].len );
        off += input[ i ].len;
    }
    one.base = buf;
    one.len = total;
    s_origEncryptV( ctx, output, &one, 1, seq, aad, aadlen );
    if( buf != stackbuf )
    {
        vPortFree( buf );
    }
}

/* Per-algorithm setup thunks: run picotls's real setup (imports the key, sets do_encrypt_v =
 * aead_encrypt_v), then swap in the coalescing encrypt_v on the encrypt context. */
static aead_setup_fn s_origSetup128, s_origSetup256, s_origSetupCP;

static int aead_wrap_setup( aead_setup_fn orig, ptls_aead_context_t * ctx, int is_enc,
                            const void * key, const void * iv )
{
    int ret = orig( ctx, is_enc, key, iv );
    if( ( ret == 0 ) && is_enc )
    {
        if( s_origEncryptV == NULL )
        {
            s_origEncryptV = ctx->do_encrypt_v;
        }
        ctx->do_encrypt_v = aead_coalesce_encrypt_v;
    }
    return ret;
}
static int aead_wrap_setup_128( ptls_aead_context_t * c, int e, const void * k, const void * iv )
{ return aead_wrap_setup( s_origSetup128, c, e, k, iv ); }
static int aead_wrap_setup_256( ptls_aead_context_t * c, int e, const void * k, const void * iv )
{ return aead_wrap_setup( s_origSetup256, c, e, k, iv ); }
static int aead_wrap_setup_cp( ptls_aead_context_t * c, int e, const void * k, const void * iv )
{ return aead_wrap_setup( s_origSetupCP, c, e, k, iv ); }

/* Non-const copies (the picotls typedefs are `const`) of the AEAD algorithms + cipher suites, with
 * setup_crypto / aead overridden to point at the wrappers. */
static struct st_ptls_aead_algorithm_t s_wrapAes128gcm, s_wrapAes256gcm, s_wrapChachapoly;
static struct st_ptls_cipher_suite_t   s_wrapAes128gcmSha256, s_wrapAes256gcmSha384, s_wrapChachapolySha256;

static void build_wrapped_suite( struct st_ptls_aead_algorithm_t * wa, ptls_aead_algorithm_t * origA,
                                 aead_setup_fn * saveSetup, aead_setup_fn wrapSetup,
                                 struct st_ptls_cipher_suite_t * wcs, ptls_cipher_suite_t * origCs )
{
    /* memcpy, not struct assignment: the algo/cipher-suite structs have const members. We then
     * write only the non-const setup_crypto / aead fields through the non-const tag pointers. */
    memcpy( wa, origA, sizeof( *wa ) );
    *saveSetup = origA->setup_crypto;
    wa->setup_crypto = wrapSetup;
    memcpy( wcs, origCs, sizeof( *wcs ) );
    wcs->aead = wa;
}
#endif /* MBEDTLS_GCM_ALT */

/* Cipher-suite list, filled at runtime by picotls_build_cipher_suites(). AES-128-GCM first
 * (HW-accelerated on the RTL8721D IPSEC engine; fewest rounds), then AES-256-GCM, ChaCha20-Poly1305
 * last — same order as the QUIC client. */
static ptls_cipher_suite_t * s_cipherSuites[ 4 ];

static void picotls_build_cipher_suites( void )
{
    size_t n = 0;
#if defined( MBEDTLS_GCM_ALT )
    build_wrapped_suite( &s_wrapAes128gcm, &ptls_mbedtls_aes128gcm, &s_origSetup128, aead_wrap_setup_128,
                         &s_wrapAes128gcmSha256, &ptls_mbedtls_aes128gcmsha256 );
    s_cipherSuites[ n++ ] = &s_wrapAes128gcmSha256;
#if defined( MBEDTLS_SHA384_C )
    build_wrapped_suite( &s_wrapAes256gcm, &ptls_mbedtls_aes256gcm, &s_origSetup256, aead_wrap_setup_256,
                         &s_wrapAes256gcmSha384, &ptls_mbedtls_aes256gcmsha384 );
    s_cipherSuites[ n++ ] = &s_wrapAes256gcmSha384;
#endif
    build_wrapped_suite( &s_wrapChachapoly, &ptls_mbedtls_chacha20poly1305, &s_origSetupCP, aead_wrap_setup_cp,
                         &s_wrapChachapolySha256, &ptls_mbedtls_chacha20poly1305sha256 );
    s_cipherSuites[ n++ ] = &s_wrapChachapolySha256;
#else
    s_cipherSuites[ n++ ] = &ptls_mbedtls_aes128gcmsha256;
#if defined( MBEDTLS_SHA384_C )
    s_cipherSuites[ n++ ] = &ptls_mbedtls_aes256gcmsha384;
#endif
    s_cipherSuites[ n++ ] = &ptls_mbedtls_chacha20poly1305sha256;
#endif
    s_cipherSuites[ n ] = NULL;
}

/* PSA-backed RNG adapter for mbedtls_pk_parse_key / _sign (reuses picotls's PSA random). */
static int picotls_rng( void * ctx, unsigned char * buf, size_t len )
{
    ( void ) ctx;
    ptls_mbedtls_random_bytes( buf, len );
    return 0;
}

/* Map a TLS 1.3 signature scheme -> (mbedTLS message digest, RSA-PSS vs ECDSA). */
static int picotls_scheme_to_md( uint16_t algo,
                                 mbedtls_md_type_t * md,
                                 int * isRsaPss )
{
    switch( algo )
    {
        case PTLS_SIGNATURE_RSA_PSS_RSAE_SHA256: *md = MBEDTLS_MD_SHA256; *isRsaPss = 1; return 0;
        case PTLS_SIGNATURE_RSA_PSS_RSAE_SHA384: *md = MBEDTLS_MD_SHA384; *isRsaPss = 1; return 0;
        case PTLS_SIGNATURE_RSA_PSS_RSAE_SHA512: *md = MBEDTLS_MD_SHA512; *isRsaPss = 1; return 0;
        case PTLS_SIGNATURE_ECDSA_SECP256R1_SHA256: *md = MBEDTLS_MD_SHA256; *isRsaPss = 0; return 0;
        default: return -1;
    }
}

/*-----------------------------------------------------------*/
/* Server-certificate verifier — ported from picotls's openssl backend
 * (lib/openssl.c: verify_cert / verify_cert_chain / verify_sign / init), OpenSSL X.509
 * calls swapped for mbedTLS equivalents. The mbedTLS picotls backend ships no verifier. */

/* Schemes we accept for the server's CertificateVerify (UINT16_MAX-terminated). */
static const uint16_t s_verifyAlgos[] = {
    PTLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
    PTLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
    PTLS_SIGNATURE_RSA_PSS_RSAE_SHA512,
    PTLS_SIGNATURE_ECDSA_SECP256R1_SHA256,
    UINT16_MAX,
};

typedef struct
{
    ptls_verify_certificate_t super;
    mbedtls_x509_crt          trustCa;   /* file/folder-loaded trust anchors (the `ca/` store) */
    int                       loaded;
} picotls_verify_t;
static picotls_verify_t s_verifier;

/* The parsed server chain (leaf = list head). Lives from verify_cert() until verify_sign()
 * runs — single connection, so a static holder avoids per-call malloc. */
static mbedtls_x509_crt s_serverChain;
static int              s_serverChainInUse;

/* verify_sign: check the TLS 1.3 CertificateVerify signature with the leaf public key, then
 * free the chain. picotls also calls this with an empty `data` purely to free (the contract
 * when an error occurs between verify_cert success and this call). */
static int picotls_verify_sign( void * verify_ctx,
                                uint16_t algo,
                                ptls_iovec_t data,
                                ptls_iovec_t sign )
{
    mbedtls_x509_crt * chain = ( mbedtls_x509_crt * ) verify_ctx; /* == &s_serverChain */
    int ret = 0;
    mbedtls_md_type_t md;
    int isRsaPss;
    const mbedtls_md_info_t * mdInfo;
    unsigned char hash[ 64 ];
    size_t hashLen;

    if( data.base == NULL )
    {
        goto Exit; /* free-only call */
    }

    if( picotls_scheme_to_md( algo, &md, &isRsaPss ) != 0 )
    {
        ret = PTLS_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }

    mdInfo = mbedtls_md_info_from_type( md );
    if( mdInfo == NULL )
    {
        ret = PTLS_ALERT_ILLEGAL_PARAMETER;
        goto Exit;
    }
    hashLen = mbedtls_md_get_size( mdInfo );

    if( mbedtls_md( mdInfo, data.base, data.len, hash ) != 0 )
    {
        ret = PTLS_ALERT_DECRYPT_ERROR;
        goto Exit;
    }

    if( isRsaPss )
    {
        mbedtls_pk_rsassa_pss_options opts;
        opts.mgf1_hash_id = md;
        opts.expected_salt_len = MBEDTLS_RSA_SALT_LEN_ANY;

        if( mbedtls_pk_verify_ext( MBEDTLS_PK_RSASSA_PSS, &opts, &chain->pk, md,
                                   hash, hashLen, sign.base, sign.len ) != 0 )
        {
            ret = PTLS_ALERT_DECRYPT_ERROR;
        }
    }
    else
    {
        if( mbedtls_pk_verify( &chain->pk, md, hash, hashLen, sign.base, sign.len ) != 0 )
        {
            ret = PTLS_ALERT_DECRYPT_ERROR;
        }
    }

Exit:
    if( chain != NULL )
    {
        mbedtls_x509_crt_free( chain );
        s_serverChainInUse = 0;
    }
    return ret;
}

/* verify_cert: parse the DER chain, verify it against the trust anchor + hostname, then hand
 * back the leaf (whole chain) + verify_sign for the CertificateVerify step. */
static int picotls_verify_cert( ptls_verify_certificate_t * _self,
                                ptls_t * tls,
                                const char * server_name,
                                int ( ** verify_sign )( void *, uint16_t, ptls_iovec_t, ptls_iovec_t ),
                                void ** verify_data,
                                ptls_iovec_t * certs,
                                size_t num_certs )
{
    picotls_verify_t * self = ( picotls_verify_t * ) _self;
    int ret;
    uint32_t flags = 0;
    size_t i;

    ( void ) tls;

    if( num_certs == 0 )
    {
        return PTLS_ALERT_CERTIFICATE_REQUIRED;
    }

    /* (re)init the server-chain holder for this handshake. */
    if( s_serverChainInUse )
    {
        mbedtls_x509_crt_free( &s_serverChain );
    }
    mbedtls_x509_crt_init( &s_serverChain );
    s_serverChainInUse = 1;

    /* AWS IoT over-sends its COMPLETE chain (leaf, Amazon RSA 2048 MXX, Amazon Root CA 1, Starfield
     * Services Root G2). On bl602's ~50 KB heap the TLS 1.3 handshake peak already leaves only ~2 KB
     * by the time the server cert is verified, so parsing all four certs (~2.7 KB parsed each) OOMs
     * the RSA-2048 verify modexp -> MPI_ALLOC_FAILED, surfaced as esp_crt_bundle "PK verify 0x4290".
     * We only need the leaf + its issuing intermediate: the trust anchor (Amazon Root CA 1) is
     * supplied by the embedded bundle / the pinned CA, and the server-sent roots are never trusted on
     * their own word anyway. Cap the parsed chain so the verify's heap peak stays within budget.
     * (leaf + one intermediate covers AWS IoT; raise PICOTLS_VERIFY_MAX_CHAIN for deeper chains.) */
    #ifndef PICOTLS_VERIFY_MAX_CHAIN
    #define PICOTLS_VERIFY_MAX_CHAIN    2U
    #endif
    size_t nparse = ( num_certs > PICOTLS_VERIFY_MAX_CHAIN ) ? ( size_t ) PICOTLS_VERIFY_MAX_CHAIN : num_certs;
    for( i = 0; i < nparse; i++ )
    {
        if( mbedtls_x509_crt_parse_der( &s_serverChain, certs[ i ].base, certs[ i ].len ) != 0 )
        {
            ret = PTLS_ALERT_BAD_CERTIFICATE;
            goto Fail;
        }
    }

    const char * cn = ( ( server_name != NULL ) && ( server_name[ 0 ] != '\0' ) ) ? server_name : NULL;
#ifdef AT_PROJECT_CRT_BUNDLE
    /* Verify against the embedded Mozilla bundle, with the file/folder roots (self->trustCa, the
     * `ca/` store) as extra trust anchors for self-signed / niche CAs the bundle lacks. */
    ret = esp_crt_bundle_verify_chain( &s_serverChain, &self->trustCa, cn, &flags );
#else
    /* No bundle: the file/folder roots are the only trust anchors. */
    ret = mbedtls_x509_crt_verify( &s_serverChain, &self->trustCa, NULL, cn, &flags, NULL, NULL );
#endif

    /* Report the server (leaf) cert identity + the verification outcome at handshake time. */
    {
        char subj[ 160 ], issuer[ 160 ];
        ( void ) mbedtls_x509_dn_gets( subj, sizeof( subj ), &s_serverChain.subject );
        ( void ) mbedtls_x509_dn_gets( issuer, sizeof( issuer ), &s_serverChain.issuer );
        LogInfo( ( "server cert Subject [%s] Issuer [%s] -- verify %s (ret=-0x%04X flags=0x%08X)",
                   subj, issuer, ( ret == 0 ) ? "PASS" : "FAIL",
                   ( unsigned int ) -ret, ( unsigned int ) flags ) );
    }

    if( ret != 0 )
    {
        LogError( ( "server certificate verification failed: -0x%04X, flags=0x%08X",
                    ( unsigned int ) -ret, ( unsigned int ) flags ) );

        if( ( flags & MBEDTLS_X509_BADCERT_REVOKED ) != 0 )
        {
            ret = PTLS_ALERT_CERTIFICATE_REVOKED;
        }
        else if( ( flags & ( MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE ) ) != 0 )
        {
            ret = PTLS_ALERT_CERTIFICATE_EXPIRED;
        }
        else if( ( flags & ( MBEDTLS_X509_BADCERT_CN_MISMATCH | MBEDTLS_X509_BADCERT_BAD_MD |
                             MBEDTLS_X509_BADCERT_BAD_PK | MBEDTLS_X509_BADCERT_BAD_KEY ) ) != 0 )
        {
            ret = PTLS_ALERT_BAD_CERTIFICATE;
        }
        else if( ( flags & ( MBEDTLS_X509_BADCERT_NOT_TRUSTED | MBEDTLS_X509_BADCERT_MISSING ) ) != 0 )
        {
            ret = PTLS_ALERT_UNKNOWN_CA;
        }
        else
        {
            ret = PTLS_ALERT_CERTIFICATE_UNKNOWN;
        }
        goto Fail;
    }

    *verify_data = &s_serverChain;
    *verify_sign = picotls_verify_sign;
    return 0;

Fail:
    mbedtls_x509_crt_free( &s_serverChain );
    s_serverChainInUse = 0;
    return ret;
}

/* Load the trust anchors from the `ca/` store (a folder -> parse_path, or a single PEM/DER file)
 * and arm the verifier. In a CRT_BUNDLE=ON build an empty/missing store is fine (the embedded
 * bundle is the trust source); in a CRT_BUNDLE=OFF build it leaves nothing to trust -> fail. */
static int picotls_init_verify( picotls_verify_t * self,
                                const char * rootCaPath )
{
    struct stat st;
    int rc = 0;

    mbedtls_x509_crt_init( &self->trustCa );

    if( rootCaPath != NULL )
    {
        if( ( stat( rootCaPath, &st ) == 0 ) && S_ISDIR( st.st_mode ) )
        {
            rc = mbedtls_x509_crt_parse_path( &self->trustCa, rootCaPath );
        }
        else
        {
            rc = mbedtls_x509_crt_parse_file( &self->trustCa, rootCaPath );
        }
        if( rc < 0 )
        {
            LogWarn( ( "no CA roots loaded from '%s': -0x%04X", rootCaPath, ( unsigned int ) -rc ) );
        }
    }

#ifndef AT_PROJECT_CRT_BUNDLE
    if( self->trustCa.raw.p == NULL )
    {
        LogError( ( "no trusted CA roots (path '%s') and no embedded bundle in this build",
                    rootCaPath ? rootCaPath : "(null)" ) );
        mbedtls_x509_crt_free( &self->trustCa );
        return -1;
    }
#endif

    self->super.cb = picotls_verify_cert;
    self->super.algos = s_verifyAlgos;
    self->loaded = 1;
    return 0;
}

/*-----------------------------------------------------------*/
/* Client-key signer (mutual TLS) — software signing via mbedtls_pk, matching the current
 * AWS behavior and producing a correct rsa_pss_rsae_* signature (mbedtls_pk_sign_ext routes
 * RSASSA-PSS through PSA_ALG_RSA_PSS, salt length = hash length, as TLS 1.3 requires). */

typedef struct
{
    ptls_sign_certificate_t super;
    mbedtls_pk_context      pk;
    int                     loaded;
} picotls_sign_t;
static picotls_sign_t s_signer;

static int picotls_sign_cb( ptls_sign_certificate_t * _self,
                            ptls_t * tls,
                            ptls_async_job_t ** async,
                            uint16_t * selected_algorithm,
                            ptls_buffer_t * outbuf,
                            ptls_iovec_t input,
                            const uint16_t * algorithms,
                            size_t num_algorithms )
{
    static const uint16_t rsaSchemes[] = {
        PTLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
        PTLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
        PTLS_SIGNATURE_RSA_PSS_RSAE_SHA512,
        UINT16_MAX,
    };
    static const uint16_t ecSchemes[] = {
        PTLS_SIGNATURE_ECDSA_SECP256R1_SHA256,
        UINT16_MAX,
    };
    picotls_sign_t * self = ( picotls_sign_t * ) _self;
    mbedtls_pk_type_t kt = mbedtls_pk_get_type( &self->pk );
    int isEc = ( ( kt == MBEDTLS_PK_ECKEY ) || ( kt == MBEDTLS_PK_ECDSA ) );
    const uint16_t * mine = isEc ? ecSchemes : rsaSchemes;
    uint16_t chosen = 0;
    const uint16_t * m;
    size_t i;
    mbedtls_md_type_t md;
    int isRsaPss;
    const mbedtls_md_info_t * mdInfo;
    unsigned char hash[ 64 ];
    size_t hashLen;
    unsigned char sig[ MBEDTLS_PK_SIGNATURE_MAX_SIZE ];
    size_t sigLen = 0;
    int ret;

    ( void ) tls;
    ( void ) async;

    /* Pick the most-preferred scheme our key supports that the peer offered. */
    for( m = mine; ( *m != UINT16_MAX ) && ( chosen == 0 ); ++m )
    {
        for( i = 0; i < num_algorithms; i++ )
        {
            if( algorithms[ i ] == *m )
            {
                chosen = *m;
                break;
            }
        }
    }
    if( chosen == 0 )
    {
        LogError( ( "no mutually-supported client signature scheme" ) );
        return PTLS_ERROR_INCOMPATIBLE_KEY;
    }

    ( void ) picotls_scheme_to_md( chosen, &md, &isRsaPss ); /* always succeeds for our schemes */
    mdInfo = mbedtls_md_info_from_type( md );
    hashLen = mbedtls_md_get_size( mdInfo );

    if( mbedtls_md( mdInfo, input.base, input.len, hash ) != 0 )
    {
        return PTLS_ERROR_NOT_AVAILABLE;
    }

    if( isRsaPss )
    {
        ret = mbedtls_pk_sign_ext( MBEDTLS_PK_RSASSA_PSS, &self->pk, md, hash, hashLen,
                                   sig, sizeof( sig ), &sigLen, picotls_rng, NULL );
    }
    else
    {
        ret = mbedtls_pk_sign( &self->pk, md, hash, hashLen,
                               sig, sizeof( sig ), &sigLen, picotls_rng, NULL );
    }
    if( ret != 0 )
    {
        LogError( ( "client signature failed: -0x%04X", ( unsigned int ) -ret ) );
        return PTLS_ERROR_INCOMPATIBLE_KEY;
    }

    if( ( ret = ptls_buffer_reserve( outbuf, sigLen ) ) != 0 )
    {
        return ret;
    }
    memcpy( outbuf->base + outbuf->off, sig, sigLen );
    outbuf->off += sigLen;
    *selected_algorithm = chosen;
    return 0;
}

/* Parse the client private key (PEM or DER) into the signer. */
static int picotls_load_key( picotls_sign_t * self,
                             const uint8_t * key,
                             size_t keyLen )
{
    int rc;

    mbedtls_pk_init( &self->pk );
    rc = mbedtls_pk_parse_key( &self->pk, key, keyLen, NULL, 0, picotls_rng, NULL );
    if( rc != 0 )
    {
        LogError( ( "failed to parse client key: -0x%04X", ( unsigned int ) -rc ) );
        mbedtls_pk_free( &self->pk );
        return rc;
    }
    self->super.cb = picotls_sign_cb;
    self->loaded = 1;
    return 0;
}

/*-----------------------------------------------------------*/
/* Client certificate chain -> ctx.certificates (the leaf DER picotls sends in Certificate). */

static mbedtls_x509_crt s_clientCert;
static int              s_clientCertInUse;
static ptls_iovec_t     s_clientCertIovecs[ 4 ];

static int picotls_load_cert_chain( ptls_context_t * ctx,
                                    const NetworkCredentials_t * creds )
{
    int rc;
    size_t n = 0;
    mbedtls_x509_crt * c;

    if( s_clientCertInUse )
    {
        mbedtls_x509_crt_free( &s_clientCert );
        s_clientCertInUse = 0;
    }
    mbedtls_x509_crt_init( &s_clientCert );

    if( creds->pClientCert != NULL )
    {
        rc = mbedtls_x509_crt_parse( &s_clientCert, creds->pClientCert, creds->clientCertSize );
    }
    else if( creds->pClientCertPath != NULL )
    {
        rc = mbedtls_x509_crt_parse_file( &s_clientCert, creds->pClientCertPath );
    }
    else
    {
        return 0; /* server-auth only — no client certificate */
    }

    if( rc != 0 )
    {
        LogError( ( "failed to parse client certificate: -0x%04X", ( unsigned int ) -rc ) );
        mbedtls_x509_crt_free( &s_clientCert );
        return rc;
    }
    s_clientCertInUse = 1;

    for( c = &s_clientCert;
         ( c != NULL ) && ( n < ( sizeof( s_clientCertIovecs ) / sizeof( s_clientCertIovecs[ 0 ] ) ) );
         c = c->next )
    {
        s_clientCertIovecs[ n ].base = c->raw.p;
        s_clientCertIovecs[ n ].len = c->raw.len;
        n++;
    }
    ctx->certificates.list = s_clientCertIovecs;
    ctx->certificates.count = n;
    return 0;
}

/*-----------------------------------------------------------*/

static int picotls_ctx_once( void )
{
    if( s_ctxReady )
    {
        return 0;
    }

    /* picotls's ptls_mbedtls backend uses PSA; init it once (the mbedTLS transport's
     * initMbedtls() would normally do this, but it isn't linked on the picotls path).
     * Idempotent. mbedTLS threading is initialized by the application at startup. */
    {
        psa_status_t _ps = psa_crypto_init();
        if( _ps != PSA_SUCCESS )
        {
            LogError( ( "psa_crypto_init failed: %d", (int) _ps ) );
            return -1;
        }
    }

    s_ptlsCtx.random_bytes = ptls_mbedtls_random_bytes;
    s_ptlsCtx.get_time = &s_ptlsGetTime;
    s_ptlsCtx.key_exchanges = ptls_mbedtls_key_exchanges; /* secp256r1 (+ x25519) */
    picotls_build_cipher_suites();                        /* HW-AEAD coalesce shim under MBEDTLS_GCM_ALT */
    s_ptlsCtx.cipher_suites = s_cipherSuites;
    /* TLS 1.3 middlebox-compatibility mode (RFC 8446 D.4): emit the dummy ChangeCipherSpec so
     * the handshake looks like TLS 1.2 on the wire. AWS IoT's front end (NLB) RSTs 1.3 handshakes
     * that omit it — openssl's TLS 1.3 client and picotls/t/cli.c send it by default. (The QUIC
     * path doesn't set this: QUIC has no ChangeCipherSpec record.) */
    s_ptlsCtx.send_change_cipher_spec = 1;
    s_ctxReady = 1;
    return 0;
}

static TlsTransportStatus_t picotls_load_credentials( const NetworkCredentials_t * creds )
{
#if !defined( PICOTLS_SKIP_CERT_VERIFY )
    if( s_verifier.loaded )
    {
        mbedtls_x509_crt_free( &s_verifier.trustCa );
        s_verifier.loaded = 0;
    }
    /* Load the `ca/` store into trustCa; picotls_verify_cert layers the embedded bundle on top
     * (CRT_BUNDLE=ON) or uses these file roots alone (CRT_BUNDLE=OFF). */
    if( picotls_init_verify( &s_verifier, creds->pRootCaPath ) != 0 )
    {
        return TLS_TRANSPORT_INVALID_CREDENTIALS;
    }
    s_ptlsCtx.verify_certificate = &s_verifier.super;
#else
    s_ptlsCtx.verify_certificate = NULL;
    LogWarn( ( "PICOTLS_SKIP_CERT_VERIFY: server certificate is NOT verified (insecure)" ) );
#endif

    /* Mutual TLS: load the client cert chain + key when both are present. */
    if( ( ( creds->pClientCert != NULL ) || ( creds->pClientCertPath != NULL ) ) &&
        ( creds->pPrivateKey != NULL ) )
    {
        if( picotls_load_cert_chain( &s_ptlsCtx, creds ) != 0 )
        {
            return TLS_TRANSPORT_INVALID_CREDENTIALS;
        }
        if( s_signer.loaded )
        {
            mbedtls_pk_free( &s_signer.pk );
            s_signer.loaded = 0;
        }
        if( picotls_load_key( &s_signer, creds->pPrivateKey, creds->privateKeySize ) != 0 )
        {
            return TLS_TRANSPORT_INVALID_CREDENTIALS;
        }
        s_ptlsCtx.sign_certificate = &s_signer.super;
    }
    else
    {
        s_ptlsCtx.certificates.list = NULL;
        s_ptlsCtx.certificates.count = 0;
        s_ptlsCtx.sign_certificate = NULL;
    }

    return TLS_TRANSPORT_SUCCESS;
}

/*-----------------------------------------------------------*/
/* Streaming helpers over TCP_Sockets_*. */

static int picotls_timed_out( TickType_t deadline )
{
    return ( int32_t ) ( xTaskGetTickCount() - deadline ) >= 0;
}

/* Send the whole buffer (handling partial writes), then reset it. 0 ok, -1 error/timeout. */
static int picotls_flush( Socket_t sock,
                          ptls_buffer_t * buf,
                          TickType_t deadline )
{
    size_t off = 0;

    while( off < buf->off )
    {
        int32_t sent = TCP_Sockets_Send( sock, buf->base + off, buf->off - off );
        if( sent < 0 )
        {
            return -1;
        }
        if( sent == 0 )
        {
            if( picotls_timed_out( deadline ) )
            {
                return -1;
            }
            continue; /* send timeout (FreeRTOS+TCP blocks up to the send timeout per call) */
        }
        off += ( size_t ) sent;
    }
    buf->off = 0;
    return 0;
}

/* Decrypt [buf,len) of ciphertext, appending plaintext to params->rxPlain. 0 ok, -1 fatal. */
static int picotls_feed_records( TlsTransportParams_t * p,
                                 const uint8_t * buf,
                                 size_t len )
{
    size_t off = 0;

    while( off < len )
    {
        size_t consumed = len - off;
        int r = ptls_receive( p->tls, &p->rxPlain, buf + off, &consumed );
        off += consumed;
        if( ( r != 0 ) && ( r != PTLS_ERROR_IN_PROGRESS ) )
        {
            LogError( ( "ptls_receive failed: %d", r ) );
            return -1;
        }
        if( consumed == 0 )
        {
            break; /* defensive: picotls guarantees progress, never spin */
        }
    }
    return 0;
}

static TlsTransportStatus_t picotls_handshake( TlsTransportParams_t * p )
{
    ptls_handshake_properties_t hsprop;
    ptls_buffer_t sendbuf;
    uint8_t sendbufSmall[ 1024 ];
    uint8_t rxCipher[ PICOTLS_RX_CIPHER_CHUNK ];
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS( PICOTLS_HANDSHAKE_TIMEOUT_MS );
    int ret;

    memset( &hsprop, 0, sizeof( hsprop ) );
    ptls_buffer_init( &sendbuf, sendbufSmall, sizeof( sendbufSmall ) );

    /* Prime the ClientHello. */
    ret = ptls_handshake( p->tls, &sendbuf, NULL, NULL, &hsprop );
    if( ret != PTLS_ERROR_IN_PROGRESS )
    {
        LogError( ( "ptls_handshake (init) failed: %d", ret ) );
        goto Fail;
    }

    while( ret == PTLS_ERROR_IN_PROGRESS )
    {
        int32_t n;
        size_t off;

        /* Flush whatever we owe the peer (ClientHello, then later the Finished). */
        if( picotls_flush( p->tcpSocket, &sendbuf, deadline ) != 0 )
        {
            LogError( ( "handshake: send failed/timed out" ) );
            goto Fail;
        }

        /* Read the next slice of the peer's flight. */
        n = TCP_Sockets_Recv( p->tcpSocket, rxCipher, sizeof( rxCipher ) );
        if( n < 0 )
        {
            LogError( ( "handshake: recv failed: %d", ( int ) n ) );
            goto Fail;
        }
        if( n == 0 )
        {
            if( picotls_timed_out( deadline ) )
            {
                LogError( ( "handshake timed out" ) );
                goto Fail;
            }
            continue;
        }

        /* Feed all received bytes (the consume loop: ptls_handshake updates `consumed`). */
        off = 0;
        while( off < ( size_t ) n )
        {
            size_t consumed = ( size_t ) n - off;
            ret = ptls_handshake( p->tls, &sendbuf, rxCipher + off, &consumed, &hsprop );
            off += consumed;
            /* The Certificate-flight step holds the parsed server chain + runs cert verify here -
             * this is the handshake heap trough (the in-call verify working set is freed by now, so
             * this slightly under-reports the true peak, but captures the held cert chain). */
            TLS_HEAP_MARK( "hs-step" );
            if( ret == 0 )
            {
                break;
            }
            if( ret != PTLS_ERROR_IN_PROGRESS )
            {
                ( void ) picotls_flush( p->tcpSocket, &sendbuf, deadline ); /* push any alert */
                LogError( ( "ptls_handshake failed: %d", ret ) );
                goto Fail;
            }
        }

        if( ret == 0 )
        {
            /* Handshake complete: flush our final flight (Finished), and decrypt any
             * trailing ciphertext (rare for a TLS 1.3 client) so recv() doesn't lose it. */
            if( picotls_flush( p->tcpSocket, &sendbuf, deadline ) != 0 )
            {
                LogError( ( "handshake: final flush failed" ) );
                goto Fail;
            }
            if( off < ( size_t ) n )
            {
                if( picotls_feed_records( p, rxCipher + off, ( size_t ) n - off ) != 0 )
                {
                    goto Fail;
                }
            }
            break;
        }
    }

    ptls_buffer_dispose( &sendbuf );
    LogInfo( ( "picotls handshake successful" ) );
    return TLS_TRANSPORT_SUCCESS;

Fail:
    ptls_buffer_dispose( &sendbuf );
    return TLS_TRANSPORT_HANDSHAKE_FAILED;
}

/*-----------------------------------------------------------*/

TlsTransportStatus_t TLS_FreeRTOS_Connect( NetworkContext_t * pNetworkContext,
                                           const char * pHostName,
                                           uint16_t port,
                                           const NetworkCredentials_t * pNetworkCredentials,
                                           uint32_t receiveTimeoutMs,
                                           uint32_t sendTimeoutMs )
{
    TlsTransportParams_t * p = NULL;
    TlsTransportStatus_t status = TLS_TRANSPORT_SUCCESS;
    BaseType_t isSocketConnected = pdFALSE, isTlsCreated = pdFALSE;

    if( ( pNetworkContext == NULL ) ||
        ( pNetworkContext->pParams == NULL ) ||
        ( pHostName == NULL ) ||
        ( pNetworkCredentials == NULL ) )
    {
        LogError( ( "invalid parameter(s): pNetworkContext=%p, pHostName=%p, pNetworkCredentials=%p",
                    ( void * ) pNetworkContext, ( const void * ) pHostName, ( const void * ) pNetworkCredentials ) );
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }
#ifndef AT_PROJECT_CRT_BUNDLE
    if( pNetworkCredentials->pRootCaPath == NULL )
    {
        /* Without the embedded bundle, the `ca/` folder is the only trust source. */
        LogError( ( "pRootCaPath cannot be NULL (no embedded CA bundle in this build)." ) );
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }
#endif

    p = pNetworkContext->pParams;
    p->tcpSocket = NULL;
    p->tls = NULL;
    p->rxConsumed = 0;

    if( picotls_ctx_once() != 0 )
    {
        return TLS_TRANSPORT_INTERNAL_ERROR;
    }

    /* TCP connection. The recv timeout set here is what makes recv()/handshake honor it. */
    if( TCP_Sockets_Connect( &p->tcpSocket, pHostName, port, receiveTimeoutMs, sendTimeoutMs ) != 0 )
    {
        LogError( ( "failed to connect to %s:%u", pHostName, ( unsigned ) port ) );
        return TLS_TRANSPORT_CONNECT_FAILURE;
    }
    isSocketConnected = pdTRUE;

    status = picotls_load_credentials( pNetworkCredentials );
    if( status != TLS_TRANSPORT_SUCCESS )
    {
        goto Cleanup;
    }

    TLS_HEAP_MARK( "pre-client_new" );
    p->tls = ptls_client_new( &s_ptlsCtx );
    if( p->tls == NULL )
    {
        LogError( ( "ptls_client_new failed" ) );
        status = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
        goto Cleanup;
    }
    ptls_buffer_init( &p->rxPlain, "", 0 );
    p->rxConsumed = 0;
    isTlsCreated = pdTRUE;

    p->rxFrame = NULL;
    p->rxFrameFilled = 0;
    p->rxFrameNeed = 0;

    /* Pre-reserve the rxPlain decrypt sink up front, while the heap is least fragmented (the decrypt
     * path otherwise reallocs rxPlain mid-stream to fit a record's plaintext, and a large realloc can
     * fail in a fragmented heap during a big transfer). Default 4 KB: enough for typical small app
     * records (MQTT control/telemetry, HTTP headers) without holding a full 16 KB record; a bigger
     * record still grows the buffer on demand. Callers with a known larger steady record (e.g. the
     * OTA HTTPS data plane) pass a matching rxReserveSize so the grow happens once, up front. */
    #define PICOTLS_RX_RESERVE_DEFAULT    ( 4096U )
    {
        size_t rxReserve = ( pNetworkCredentials->rxReserveSize != 0 )
                           ? pNetworkCredentials->rxReserveSize : PICOTLS_RX_RESERVE_DEFAULT;
    if( ptls_buffer_reserve( &p->rxPlain, rxReserve ) != 0 )
    {
        LogError( ( "rxPlain pre-reserve failed" ) );
        status = TLS_TRANSPORT_INSUFFICIENT_MEMORY;
        goto Cleanup;
    }

    /* Record-framing (see TLS_FreeRTOS_recv) is opt-in per connection via a LARGE rxReserveSize: only
     * a bulk-download caller (ota_http, rxReserveSize=16640) wants the full-record buffer, because it
     * costs a resident 16 KB and its payoff — no per-record recvbuf.rec malloc/free churn — only
     * matters over a long transfer. A small-record connection (MQTT/telemetry, tiny reserve) keeps the
     * old 2 KB-chunk path (rxFrame==NULL): heap-tight parts like bl602 (~35 KB free) CANNOT spare 16 KB
     * for their MQTT session, and their few control records don't fragment. The alloc is best-effort —
     * if it fails, fall back to the chunk path rather than failing the connection. */
    if( rxReserve >= PICOTLS_FRAME_MIN_RESERVE )
    {
        p->rxFrame = pvPortMalloc( PICOTLS_MAX_RECORD );
        if( p->rxFrame == NULL )
        {
            LogWarn( ( "rxFrame alloc failed; falling back to the chunked recv path" ) );
        }
    }
    }
    TLS_HEAP_MARK( "post-client_new+reserve" );

    if( pNetworkCredentials->disableSni == pdFALSE )
    {
        ptls_set_server_name( p->tls, pHostName, 0 );
    }
    if( pNetworkCredentials->pAlpnProtos != NULL )
    {
        LogWarn( ( "picotls path: ALPN not wired; ignoring (AWS IoT 8883 needs none)" ) );
    }

    status = picotls_handshake( p );
    if( status != TLS_TRANSPORT_SUCCESS )
    {
        goto Cleanup;
    }
    TLS_HEAP_MARK( "post-handshake" );

    LogInfo( ( "(Network connection %p) Connection to %s established.",
               ( void * ) pNetworkContext, pHostName ) );
    return TLS_TRANSPORT_SUCCESS;

Cleanup:
    if( isTlsCreated == pdTRUE )
    {
        ptls_buffer_dispose( &p->rxPlain );
        if( p->rxFrame != NULL )
        {
            vPortFree( p->rxFrame );
            p->rxFrame = NULL;
        }
        ptls_free( p->tls );
        p->tls = NULL;
    }
    if( isSocketConnected == pdTRUE )
    {
        TCP_Sockets_Disconnect( p->tcpSocket );
        p->tcpSocket = NULL;
    }
    return status;
}
/*-----------------------------------------------------------*/

void TLS_FreeRTOS_Disconnect( NetworkContext_t * pNetworkContext )
{
    TlsTransportParams_t * p = NULL;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        return;
    }
    p = pNetworkContext->pParams;

    /* Best-effort close_notify. */
    if( ( p->tls != NULL ) && ( p->tcpSocket != NULL ) )
    {
        ptls_buffer_t alert;
        uint8_t alertSmall[ 32 ];
        ptls_buffer_init( &alert, alertSmall, sizeof( alertSmall ) );
        if( ( ptls_send_alert( p->tls, &alert, PTLS_ALERT_LEVEL_WARNING, PTLS_ALERT_CLOSE_NOTIFY ) == 0 ) &&
            ( alert.off > 0 ) )
        {
            ( void ) TCP_Sockets_Send( p->tcpSocket, alert.base, alert.off );
        }
        ptls_buffer_dispose( &alert );
    }

    if( p->tcpSocket != NULL )
    {
        TCP_Sockets_Disconnect( p->tcpSocket );
        p->tcpSocket = NULL;
    }
    if( p->tls != NULL )
    {
        ptls_free( p->tls );
        p->tls = NULL;
    }
    ptls_buffer_dispose( &p->rxPlain );
    p->rxConsumed = 0;
    if( p->rxFrame != NULL )
    {
        vPortFree( p->rxFrame );
        p->rxFrame = NULL;
    }
    p->rxFrameFilled = 0;
    p->rxFrameNeed = 0;

    /* Release per-connection credential state (statics shared across the single connection). */
    if( s_signer.loaded )
    {
        mbedtls_pk_free( &s_signer.pk );
        s_signer.loaded = 0;
    }
    if( s_clientCertInUse )
    {
        mbedtls_x509_crt_free( &s_clientCert );
        s_clientCertInUse = 0;
    }
#if !defined( PICOTLS_SKIP_CERT_VERIFY )
    if( s_verifier.loaded )
    {
        mbedtls_x509_crt_free( &s_verifier.trustCa );
        s_verifier.loaded = 0;
    }
#endif
    if( s_serverChainInUse )
    {
        mbedtls_x509_crt_free( &s_serverChain );
        s_serverChainInUse = 0;
    }
    TLS_HEAP_MARK( "post-disconnect" );
}
/*-----------------------------------------------------------*/

int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv )
{
    TlsTransportParams_t * p = NULL;
    int32_t n;
    size_t avail, take;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) || ( pBuffer == NULL ) || ( bytesToRecv == 0 ) )
    {
        LogError( ( "invalid input to recv" ) );
        return -1;
    }
    p = pNetworkContext->pParams;

    /* (A) Serve buffered plaintext from a previous record first. */
    if( p->rxConsumed < p->rxPlain.off )
    {
        avail = p->rxPlain.off - p->rxConsumed;
        take = ( avail < bytesToRecv ) ? avail : bytesToRecv;
        memcpy( pBuffer, p->rxPlain.base + p->rxConsumed, take );
        p->rxConsumed += take;
        if( p->rxConsumed == p->rxPlain.off )
        {
            p->rxPlain.off = 0;
            p->rxConsumed = 0;
        }
        return ( int32_t ) take;
    }

    /* (B) Buffer drained — read the next record's plaintext into rxPlain. */
    p->rxPlain.off = 0;
    p->rxConsumed = 0;

    if( p->rxFrame != NULL )
    {
        /* Record-framing path (bulk download, e.g. ota_http): assemble exactly ONE whole TLS record
         * into rxFrame — read the 5-byte header (length = bytes[3..4], big-endian), then the body,
         * reading record-aligned amounts so we never straddle into the next record — then feed the
         * COMPLETE record to picotls so it takes the zero-alloc fast path (no per-record recvbuf.rec
         * malloc/free churn that would fragment a tight heap over a long download into
         * PTLS_ERROR_NO_MEMORY). A mid-record recv timeout returns 0 with the partial record kept in
         * p->rxFrame{Filled,Need} for the next call. */
        for( ; ; )
        {
            size_t want = ( p->rxFrameFilled < 5U )
                          ? ( 5U - p->rxFrameFilled )                 /* still filling the record header */
                          : ( p->rxFrameNeed - p->rxFrameFilled );    /* header parsed: fill the body     */

            n = TCP_Sockets_Recv( p->tcpSocket, p->rxFrame + p->rxFrameFilled, want );
            if( n == 0 )
            {
                return 0; /* timeout — partial record kept for the next call; caller retries */
            }
            if( n < 0 )
            {
                LogError( ( "recv: socket error %d", ( int ) n ) );
                return -1;
            }
            p->rxFrameFilled += ( size_t ) n;

            if( ( p->rxFrameNeed == 0U ) && ( p->rxFrameFilled >= 5U ) )
            {
                p->rxFrameNeed = 5U + ( ( (size_t) p->rxFrame[ 3 ] << 8 ) | (size_t) p->rxFrame[ 4 ] );
                if( p->rxFrameNeed > PICOTLS_MAX_RECORD )
                {
                    LogError( ( "recv: TLS record too large (%u)", (unsigned) p->rxFrameNeed ) );
                    return -1;
                }
            }
            if( ( p->rxFrameNeed != 0U ) && ( p->rxFrameFilled >= p->rxFrameNeed ) )
            {
                break; /* a whole record is in rxFrame */
            }
        }

        /* (C) Decrypt the complete record — ptls_receive fast-paths it (recvbuf.rec stays NULL). */
        {
            size_t recBytes = p->rxFrameNeed;
            p->rxFrameFilled = 0;
            p->rxFrameNeed = 0;
            if( picotls_feed_records( p, p->rxFrame, recBytes ) != 0 )
            {
                return -1;
            }
        }
    }
    else
    {
        /* Chunked path (small-record connections, e.g. MQTT/telemetry, and any part too heap-tight for
         * the frame buffer): read one MTU-ish ciphertext chunk and let picotls reassemble records
         * internally. The per-record recvbuf.rec churn is negligible for a handful of control records,
         * and this avoids holding a resident 16 KB buffer that bl602 (~35 KB free) can't spare. */
        uint8_t rxCipher[ PICOTLS_RX_CIPHER_CHUNK ];

        n = TCP_Sockets_Recv( p->tcpSocket, rxCipher, sizeof( rxCipher ) );
        if( n == 0 )
        {
            return 0; /* timeout, no data — caller retries */
        }
        if( n < 0 )
        {
            LogError( ( "recv: socket error %d", ( int ) n ) );
            return -1;
        }
        if( picotls_feed_records( p, rxCipher, ( size_t ) n ) != 0 )
        {
            return -1;
        }
    }

    if( p->rxPlain.off == 0 )
    {
        return 0; /* only non-application records (e.g. NewSessionTicket) — retry */
    }

    /* (D) Hand back up to bytesToRecv; stash the rest. */
    take = ( p->rxPlain.off < bytesToRecv ) ? p->rxPlain.off : bytesToRecv;
    memcpy( pBuffer, p->rxPlain.base, take );
    p->rxConsumed = take;
    if( p->rxConsumed == p->rxPlain.off )
    {
        p->rxPlain.off = 0;
        p->rxConsumed = 0;
    }
    return ( int32_t ) take;
}
/*-----------------------------------------------------------*/

int32_t TLS_FreeRTOS_send( NetworkContext_t * pNetworkContext,
                           const void * pBuffer,
                           size_t bytesToSend )
{
    TlsTransportParams_t * p = NULL;
    ptls_buffer_t enc;
    uint8_t encSmall[ 1024 ];
    size_t off = 0;
    int32_t result;

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) || ( pBuffer == NULL ) || ( bytesToSend == 0 ) )
    {
        LogError( ( "invalid input to send" ) );
        return -1;
    }
    p = pNetworkContext->pParams;

    ptls_buffer_init( &enc, encSmall, sizeof( encSmall ) );

    if( ptls_send( p->tls, &enc, pBuffer, bytesToSend ) != 0 )
    {
        LogError( ( "ptls_send failed" ) );
        ptls_buffer_dispose( &enc );
        return -1;
    }

    /* The whole record set must reach the wire (or we error) — a short plaintext count
     * would desync the TLS record stream, so we never report a partial success. */
    result = ( int32_t ) bytesToSend;
    while( off < enc.off )
    {
        int32_t sent = TCP_Sockets_Send( p->tcpSocket, enc.base + off, enc.off - off );
        if( sent < 0 )
        {
            result = -1;
            break;
        }
        if( sent == 0 )
        {
            if( off == 0 )
            {
                result = 0; /* send timeout before any bytes — caller retries the whole call */
                break;
            }
            continue; /* mid-record timeout: keep trying (TCP_Sockets_Send blocks per call) */
        }
        off += ( size_t ) sent;
    }

    ptls_buffer_dispose( &enc );
    return result;
}
/*-----------------------------------------------------------*/
