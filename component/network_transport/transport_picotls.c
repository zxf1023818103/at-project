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

/*-----------------------------------------------------------*/

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

/*-----------------------------------------------------------*/
/* TLS handshake wall-clock cap (matches the mbedTLS transport). */
#define PICOTLS_HANDSHAKE_TIMEOUT_MS    ( 10000U )

/* Ciphertext read chunk for the handshake / record loops. picotls reassembles records
 * larger than this internally, so an MTU-ish chunk is plenty (and keeps the stack small). */
#define PICOTLS_RX_CIPHER_CHUNK         ( 2048U )

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

/* Cipher-suite preference: AES-128-GCM first (HW-accelerated on the RTL8721D IPSEC engine),
 * then AES-256-GCM, ChaCha20-Poly1305 last as a fallback — same order as the QUIC client. */
static ptls_cipher_suite_t * s_cipherSuites[] = {
    &ptls_mbedtls_aes128gcmsha256,
#if defined( MBEDTLS_SHA384_C )
    &ptls_mbedtls_aes256gcmsha384,
#endif
    &ptls_mbedtls_chacha20poly1305sha256,
    NULL,
};

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
    mbedtls_x509_crt          trustCa;   /* parsed trust anchor (the root CA in the creds) */
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

    for( i = 0; i < num_certs; i++ )
    {
        if( mbedtls_x509_crt_parse_der( &s_serverChain, certs[ i ].base, certs[ i ].len ) != 0 )
        {
            ret = PTLS_ALERT_BAD_CERTIFICATE;
            goto Fail;
        }
    }

    ret = mbedtls_x509_crt_verify( &s_serverChain, &self->trustCa, NULL,
                                   ( ( server_name != NULL ) && ( server_name[ 0 ] != '\0' ) ) ? server_name : NULL,
                                   &flags, NULL, NULL );
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

/* Parse the trust anchor and arm the verifier. PEM (rootCaSize includes the NUL) or DER. */
static int picotls_init_verify( picotls_verify_t * self,
                                const uint8_t * rootCa,
                                size_t rootCaSize )
{
    int rc;

    mbedtls_x509_crt_init( &self->trustCa );
    rc = mbedtls_x509_crt_parse( &self->trustCa, rootCa, rootCaSize );
    if( rc != 0 )
    {
        LogError( ( "failed to parse root CA: -0x%04X", ( unsigned int ) -rc ) );
        mbedtls_x509_crt_free( &self->trustCa );
        return rc;
    }
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
    if( psa_crypto_init() != PSA_SUCCESS )
    {
        LogError( ( "psa_crypto_init failed" ) );
        return -1;
    }

    s_ptlsCtx.random_bytes = ptls_mbedtls_random_bytes;
    s_ptlsCtx.get_time = &s_ptlsGetTime;
    s_ptlsCtx.key_exchanges = ptls_mbedtls_key_exchanges; /* secp256r1 (+ x25519) */
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
    if( picotls_init_verify( &s_verifier, creds->pRootCa, creds->rootCaSize ) != 0 )
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
    if( pNetworkCredentials->pRootCa == NULL )
    {
        LogError( ( "pRootCa cannot be NULL." ) );
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }

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

    LogInfo( ( "(Network connection %p) Connection to %s established.",
               ( void * ) pNetworkContext, pHostName ) );
    return TLS_TRANSPORT_SUCCESS;

Cleanup:
    if( isTlsCreated == pdTRUE )
    {
        ptls_buffer_dispose( &p->rxPlain );
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
}
/*-----------------------------------------------------------*/

int32_t TLS_FreeRTOS_recv( NetworkContext_t * pNetworkContext,
                           void * pBuffer,
                           size_t bytesToRecv )
{
    TlsTransportParams_t * p = NULL;
    uint8_t rxCipher[ PICOTLS_RX_CIPHER_CHUNK ];
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

    /* (B) Buffer drained — reset and read one ciphertext chunk (honors the recv timeout). */
    p->rxPlain.off = 0;
    p->rxConsumed = 0;

    n = TCP_Sockets_Recv( p->tcpSocket, rxCipher, sizeof( rxCipher ) );
    if( n == 0 )
    {
        return 0; /* timeout, no data — coreMQTT retries */
    }
    if( n < 0 )
    {
        LogError( ( "recv: socket error %d", ( int ) n ) );
        return -1;
    }

    /* (C) Decrypt. */
    if( picotls_feed_records( p, rxCipher, ( size_t ) n ) != 0 )
    {
        return -1;
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
