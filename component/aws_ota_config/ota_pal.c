/*
 * ota_pal.c - FreeRTOS OTA PAL for the AWS IoT OTA Agent (make AWS=ON). Shared by AmebaD (RTL8721D)
 * and Bouffalo (bl602/bl616); the slot mechanics come from the per-platform ota_flash implementation.
 *
 * Bridges the OTA Agent's OtaPalInterface_t to the in-tree slot writer (ota_flash.h). The OTA *file*
 * downloaded from AWS IS the build's OTA_All.bin (32-byte Realtek OTA header + image2[+IMG3]); the
 * agent delivers it block-by-block (otaconfigFILE_BLOCK_SIZE). closeFile() verifies the AWS code-sign
 * ECDSA signature (SHA-256 over the whole downloaded OTA_All.bin, header + image) against the embedded
 * signer cert, then arms the new slot. The AWS Signer job must sign the SAME OTA_All.bin end-to-end.
 *
 * Two write strategies, selected by OTA_PAL_READBACK_VERIFY (ota_config.h):
 *
 *   Streaming (Bouffalo, =0): each block is fed IN ORDER to ota_flash_feed() (sequential slot writer +
 *     accumulated checksum) while the code-sign SHA-256 is hashed in arrival order. Out-of-order (a
 *     reconnect resume) fails the image. No read-back (would break an RSIP-encrypted image, whose
 *     signature is over the downloaded ciphertext).
 *
 *   Random-write + read-back (AmebaD, =1): each block is programmed at its offset in ANY order
 *     (ota_flash_write_at into a pre-erased slot). closeFile() then reads the whole image back
 *     (ota_flash_read_image, RSIP-OFF plaintext) to compute BOTH the code-sign hash and the checksum
 *     (ota_flash_finish_random) -- so a WiFi-reconnect reorder can no longer fail the download, and no
 *     reorder buffer is needed. REQUIRES RSIP=OFF (read-back == the signed plaintext).
 *
 * The signer cert is embedded (below); the OTA job's pCertFilepath is ignored.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ota.h"
#include "ota_private.h"
#include "ota_platform_interface.h"

#include "ota_flash.h"

#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/md.h"

#define OTA_PAL_LOG( ... )    do { printf( "[OTAPAL] " ); printf( __VA_ARGS__ ); printf( "\r\n" ); } while( 0 )

/* The OTA job-document key under which the file's code-sign signature is carried.
 * ota.c references this symbol (declared extern in ota.h) but does not define it - the
 * PAL must. AWS Signer / OTA jobs created with an ECDSA-P256 + SHA-256 signing profile
 * use "sig-sha256-ecdsa" (matches prvVerifySignature: SHA-256 + mbedtls_pk_verify over
 * the cert's EC public key). Keep in sync with the signing profile used to create jobs. */
const char OTA_JsonFileSignatureKey[ OTA_FILE_SIG_KEY_STR_MAX_LENGTH ] = "sig-sha256-ecdsa";

/* The signature-verify hash. */
#define OTA_PAL_SHA256_LEN    ( 32U )

/* Single in-flight file context state (OTA_MAX_FILES == 1). */
static ota_ctx_t          s_slot;
static int                s_active;       /* a file is currently open */

#if !OTA_PAL_READBACK_VERIFY
/* Streaming path (Bouffalo, OTA_PAL_READBACK_VERIFY=0): the sequential slot writer (ota_flash_feed) and
 * the code-sign SHA-256 are both hashed in ARRIVAL order, so blocks must arrive in order. The AmebaD
 * random-write + read-back path (=1) needs neither of these -- it hashes from the read-back at close. */
static mbedtls_sha256_context s_sha;
static uint32_t           s_consumed;     /* bytes fed to ota_flash_feed so far (== expected next offset) */

/* Feed one block to the sequential slot writer + the rolling signature hash, advancing s_consumed.
 * The caller guarantees offset == s_consumed (in order). */
static int prvFeedInOrder( const uint8_t * pData, uint32_t blockSize )
{
    if( ota_flash_feed( &s_slot, pData, blockSize ) != 0 )
    {
        return -1;
    }
    ( void ) mbedtls_sha256_update( &s_sha, pData, blockSize );
    s_consumed += blockSize;
    return 0;
}
#endif /* !OTA_PAL_READBACK_VERIFY */

/* Reset all PAL state. */
static void prvPalReset( void )
{
    memset( &s_slot, 0, sizeof( s_slot ) );
    s_active = 0;
#if !OTA_PAL_READBACK_VERIFY
    mbedtls_sha256_free( &s_sha );
    s_consumed = 0;
#endif
}

/* ----------------------------------------------------------------- create ---- */
static OtaPalStatus_t prvAbort( OtaFileContext_t * const pFileContext )
{
    ( void ) pFileContext;

    if( s_active )
    {
        OTA_PAL_LOG( "abort: discarding in-progress download" );
    }
    prvPalReset();
    ota_lock_release( OTA_SRC_AWS );

    if( pFileContext != NULL )
    {
        pFileContext->pFile = NULL;
    }
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

static OtaPalStatus_t prvCreateFileForRx( OtaFileContext_t * const pFileContext )
{
    if( pFileContext == NULL )
    {
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    prvPalReset();

    /* Anti-conflict: refuse the AWS OTA job while a BLE (mcumgr) or HTTP OTA owns the slot. */
    if( ota_lock_try( OTA_SRC_AWS ) != 0 )
    {
        OTA_PAL_LOG( "createFile: rejected - a BLE/HTTP OTA is in progress" );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

#if OTA_PAL_READBACK_VERIFY
    /* AmebaD: random-write into a lazily-erased slot; the hash + checksum come from the read-back at
     * close, so no rolling SHA is started here. Requires RSIP=OFF (read-back == the signed plaintext). */
    if( ota_flash_begin_random( &s_slot ) != 0 )
    {
        OTA_PAL_LOG( "createFile: ota_flash_begin_random FAIL" );
        ota_lock_release( OTA_SRC_AWS );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }
#else
    /* Bouffalo: sequential stream + rolling code-sign SHA-256. */
    if( ota_flash_begin( &s_slot ) != 0 )
    {
        OTA_PAL_LOG( "createFile: ota_flash_begin FAIL" );
        ota_lock_release( OTA_SRC_AWS );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    mbedtls_sha256_init( &s_sha );
    if( mbedtls_sha256_starts( &s_sha, 0 /* SHA-256, not 224 */ ) != 0 )
    {
        OTA_PAL_LOG( "createFile: sha256_starts FAIL" );
        ota_lock_release( OTA_SRC_AWS );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }
    s_consumed = 0;
#endif

    s_active = 1;

    /* The agent checks pFile for non-NULL before calling writeBlock/closeFile;
     * use a non-NULL sentinel (we keep all real state in file scope). */
    pFileContext->pFile = ( uint8_t * ) &s_slot;

    OTA_PAL_LOG( "createFile: slot write_off=0x%08x active_off=0x%08x fileSize=%u",
                 ( unsigned ) s_slot.write_off, ( unsigned ) s_slot.active_off,
                 ( unsigned ) pFileContext->fileSize );
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/* ------------------------------------------------------------- write block --- */
#if OTA_PAL_READBACK_VERIFY
/* AmebaD: random-access write -- program each block at its offset, in ANY order, into the pre-erased
 * slot. The 32-bit checksum and the code-sign hash are computed later from the read-back (prvCloseFile),
 * so there is no in-order constraint and no reorder buffering. A re-sent block is idempotent. */
static int16_t prvWriteBlock( OtaFileContext_t * const pFileContext,
                              uint32_t offset,
                              uint8_t * const pData,
                              uint32_t blockSize )
{
    ( void ) pFileContext;

    if( !s_active )
    {
        OTA_PAL_LOG( "writeBlock: no active file" );
        return -1;
    }
    if( ota_flash_write_at( &s_slot, offset, pData, blockSize ) != 0 )
    {
        OTA_PAL_LOG( "writeBlock: write_at FAIL at offset=%u", ( unsigned ) offset );
        return -1;
    }
    return ( int16_t ) blockSize;
}
#else /* streaming (Bouffalo): strict in-order feed into the sequential slot writer + rolling hash. */
static int16_t prvWriteBlock( OtaFileContext_t * const pFileContext,
                              uint32_t offset,
                              uint8_t * const pData,
                              uint32_t blockSize )
{
    ( void ) pFileContext;

    if( !s_active )
    {
        OTA_PAL_LOG( "writeBlock: no active file" );
        return -1;
    }
    if( offset < s_consumed )   /* duplicate / re-sent block after a reconnect: ack without re-writing. */
    {
        return ( int16_t ) blockSize;
    }
    if( offset > s_consumed )
    {
        OTA_PAL_LOG( "writeBlock: OUT-OF-ORDER offset=%u expected=%u (sequential PAL)",
                     ( unsigned ) offset, ( unsigned ) s_consumed );
        return -1;
    }
    if( prvFeedInOrder( pData, blockSize ) != 0 )
    {
        OTA_PAL_LOG( "writeBlock: feed FAIL at offset=%u", ( unsigned ) offset );
        return -1;
    }
    return ( int16_t ) blockSize;
}
#endif /* OTA_PAL_READBACK_VERIFY */

/* ------------------------------------------------ signature verify (close) --- */
/* The at-project OTA code-signing certificate (ECDSA P-256), embedded so no LFS cert provisioning is
 * needed. The OTA job's certfile field is ignored; the AWS Signer signature is verified against this
 * public key. Matches the ACM cert imported for the 'atProjectOta' Signer profile. */
static const char s_otaSignerCertPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBtDCCAVqgAwIBAgIUUwqQPh5f4VOCEwv6MgnnvUXTBygwCgYIKoZIzj0EAwIw\n"
    "JTEjMCEGA1UEAwwaYXQtcHJvamVjdCBPVEEgY29kZSBzaWduZXIwHhcNMjYwNjI2\n"
    "MDAxMDI2WhcNMzYwNjIzMDAxMDI2WjAlMSMwIQYDVQQDDBphdC1wcm9qZWN0IE9U\n"
    "QSBjb2RlIHNpZ25lcjBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABGmXBRO3KdqI\n"
    "eAfjRCkxonL7QDNtdLXv6jyxRjcaOiw/S4MKdbzYS01c4h2op/ywKdyeDC/tA7TG\n"
    "o7US8O4iTaKjaDBmMB0GA1UdDgQWBBQqD8y08yV90fospXSRsCjBPcou1DAfBgNV\n"
    "HSMEGDAWgBQqD8y08yV90fospXSRsCjBPcou1DAPBgNVHRMBAf8EBTADAQH/MBMG\n"
    "A1UdJQQMMAoGCCsGAQUFBwMDMAoGCCqGSM49BAMCA0gAMEUCIQDaBfe+7XR7isMJ\n"
    "fqN9rjY3Cf54woRjDhMEVHZSZyN5UQIgE1Tx8ReOF70MJmnPccPilYxuzakFcnfJ\n"
    "ZBLWLzi7u/Y=\n"
    "-----END CERTIFICATE-----\n";

/* Verify the AWS code-sign ECDSA signature against @p hash (SHA-256 over the whole downloaded image;
 * the caller computes it -- streaming: rolling s_sha; read-back: re-hashed from flash at close). */
static OtaPalStatus_t prvVerifySignature( OtaFileContext_t * const pFileContext, const uint8_t * hash )
{
    int rc;
    mbedtls_x509_crt crt;
    OtaPalStatus_t status = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );

    if( ( pFileContext->pSignature == NULL ) || ( pFileContext->pSignature->size == 0U ) )
    {
        OTA_PAL_LOG( "verify: no signature in job" );
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
    }
    ( void ) pFileContext->pCertFilepath;   /* ignored: the code-sign cert is embedded above */

    /* Parse the EMBEDDED code-signing cert (no LFS provisioning needed). */
    mbedtls_x509_crt_init( &crt );
    rc = mbedtls_x509_crt_parse( &crt, ( const unsigned char * ) s_otaSignerCertPem,
                                 sizeof( s_otaSignerCertPem ) );
    if( rc != 0 )
    {
        OTA_PAL_LOG( "verify: x509_crt_parse(embedded) FAIL -0x%04x", ( unsigned ) -rc );
        mbedtls_x509_crt_free( &crt );
        return OTA_PAL_COMBINE_ERR( OtaPalBadSignerCert, ( uint32_t ) ( -rc ) );
    }

    rc = mbedtls_pk_verify( &crt.pk, MBEDTLS_MD_SHA256,
                            hash, OTA_PAL_SHA256_LEN,
                            pFileContext->pSignature->data,
                            pFileContext->pSignature->size );
    if( rc != 0 )
    {
        OTA_PAL_LOG( "verify: pk_verify FAIL -0x%04x (sig %u bytes)",
                     ( unsigned ) -rc, ( unsigned ) pFileContext->pSignature->size );
        status = OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, ( uint32_t ) ( -rc ) );
    }
    else
    {
        OTA_PAL_LOG( "verify: signature OK" );
    }

    mbedtls_x509_crt_free( &crt );
    return status;
}

static OtaPalStatus_t prvCloseFile( OtaFileContext_t * const pFileContext )
{
    OtaPalStatus_t sigStatus;
    uint8_t        hash[ OTA_PAL_SHA256_LEN ];

    if( !s_active )
    {
        OTA_PAL_LOG( "closeFile: no active file" );
        return OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
    }

    /* Compute the code-sign hash over the whole downloaded image (header + payload). */
#if OTA_PAL_READBACK_VERIFY
    /* Random-write path: the download arrived out of order, so re-hash from the slot read-back
     * (RSIP-OFF plaintext, with the held header + boot sig spliced in). */
    {
        mbedtls_sha256_context sha;
        uint8_t  buf[ 256 ];
        uint32_t off, total = ( uint32_t ) sizeof( s_slot.hdr ) + s_slot.image_len;
        int      rc = 0;

        mbedtls_sha256_init( &sha );
        if( mbedtls_sha256_starts( &sha, 0 /* SHA-256 */ ) != 0 )
        {
            rc = -1;
        }
        for( off = 0; ( rc == 0 ) && ( off < total ); )
        {
            uint32_t n = total - off;
            if( n > sizeof( buf ) ) { n = sizeof( buf ); }
            if( ota_flash_read_image( &s_slot, off, buf, n ) != 0 ) { rc = -1; break; }
            if( mbedtls_sha256_update( &sha, buf, n ) != 0 ) { rc = -1; break; }
            off += n;
        }
        if( ( rc == 0 ) && ( mbedtls_sha256_finish( &sha, hash ) != 0 ) ) { rc = -1; }
        mbedtls_sha256_free( &sha );
        if( rc != 0 )
        {
            OTA_PAL_LOG( "closeFile: read-back hash FAIL" );
            prvPalReset();
            ota_lock_release( OTA_SRC_AWS );
            if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
            return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
        }
    }
#else
    /* Streaming path: finalize the rolling hash accumulated in writeBlock. */
    if( mbedtls_sha256_finish( &s_sha, hash ) != 0 )
    {
        OTA_PAL_LOG( "closeFile: sha256_finish FAIL" );
        prvPalReset();
        ota_lock_release( OTA_SRC_AWS );
        if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
    }
#endif

    /* Verify the code-sign signature first (mbedTLS). */
    sigStatus = prvVerifySignature( pFileContext, hash );
    if( OTA_PAL_MAIN_ERR( sigStatus ) != OtaPalSuccess )
    {
        /* Leave the slot un-armed: finish writes the boot sig last, so skipping it means the
         * bootloader will not pick this (un-finished) slot. */
        prvPalReset();
        ota_lock_release( OTA_SRC_AWS );
        if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
        return sigStatus;
    }

    /* Signature good: verify length+checksum and arm the new slot (writes boot sig last). */
#if OTA_PAL_READBACK_VERIFY
    if( ota_flash_finish_random( &s_slot ) != 0 )
#else
    if( ota_flash_finish( &s_slot ) != 0 )
#endif
    {
        OTA_PAL_LOG( "closeFile: ota_flash_finish (len/checksum) FAIL" );
        prvPalReset();
        ota_lock_release( OTA_SRC_AWS );
        if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
        return OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
    }

    OTA_PAL_LOG( "closeFile: image written + armed (%u bytes)", ( unsigned ) s_slot.image_len );
    /* Mark the freshly-armed image as pending-commit so that after the activate-reboot
     * getPlatformImageState() reports PendingCommit and the OTA library runs the self-test (else it
     * rejects: "job in self-test state while platform is not"). Persisted in KV/LFS across reboot;
     * cleared on Accepted/Rejected. */
    ( void ) setenv( "OTA_SELFTEST", "1", 1 );
    s_active = 0;
    ota_lock_release( OTA_SRC_AWS );
    if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

/* ----------------------------------------------------- activate / reset ------ */
static OtaPalStatus_t prvActivateNewImage( OtaFileContext_t * const pFileContext )
{
    ( void ) pFileContext;
    OTA_PAL_LOG( "activate: rebooting into new slot" );
    /* The new slot's boot signature was armed by ota_flash_finish(); the bootloader
     * re-selects it on reset. No A/B self-test flow (see design note). */
    ota_flash_reboot();
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 ); /* not reached */
}

static OtaPalStatus_t prvResetDevice( OtaFileContext_t * const pFileContext )
{
    ( void ) pFileContext;
    OTA_PAL_LOG( "reset: rebooting" );
    ota_flash_reboot();
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 ); /* not reached */
}

/* ------------------------------------------------- platform image state ------ */
/* SIMPLIFICATION: no self-test / rollback flow. After activate+reboot the new image
 * is treated as immediately valid; setPlatformImageState is a no-op. Document + revisit
 * if an A/B commit/rollback is wanted (would need ota_flash to track a pending-commit
 * marker and a watchdog-backed self-test window). */
static OtaPalStatus_t prvSetPlatformImageState( OtaFileContext_t * const pFileContext,
                                                OtaImageState_t eState )
{
    ( void ) pFileContext;
    /* Self-test outcome: clear the pending-commit flag so the next boot reports Valid (committed).
     * (No A/B rollback here - a Rejected image stays armed; ota_flash has no rollback path.) */
    if( ( eState == OtaImageStateAccepted ) || ( eState == OtaImageStateRejected ) ||
        ( eState == OtaImageStateAborted ) )
    {
        ( void ) setenv( "OTA_SELFTEST", "0", 1 );
        OTA_PAL_LOG( "setPlatformImageState(%d): self-test flag cleared (%s)", ( int ) eState,
                     ( eState == OtaImageStateAccepted ) ? "committed" : "rejected/aborted" );
    }
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

static OtaPalImageState_t prvGetPlatformImageState( OtaFileContext_t * const pFileContext )
{
    const char * v = getenv( "OTA_SELFTEST" );
    ( void ) pFileContext;
    /* A freshly-OTA'd image (closeFile set OTA_SELFTEST=1) reports PendingCommit until the self-test
     * commits it (-> setPlatformImageState clears the flag); any other boot is committed -> Valid. */
    if( ( v != NULL ) && ( v[ 0 ] == '1' ) )
    {
        return OtaPalImageStatePendingCommit;
    }
    return OtaPalImageStateValid;
}

/* ------------------------------------------------------------- interface ----- */
const OtaPalInterface_t * ota_pal_get_interface( void )
{
    static const OtaPalInterface_t pal =
    {
        .abort                 = prvAbort,
        .createFile            = prvCreateFileForRx,
        .closeFile             = prvCloseFile,
        .writeBlock            = prvWriteBlock,
        .activate              = prvActivateNewImage,
        .reset                 = prvResetDevice,
        .setPlatformImageState = prvSetPlatformImageState,
        .getPlatformImageState = prvGetPlatformImageState,
    };
    return &pal;
}
