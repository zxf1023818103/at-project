/*
 * ota_pal.c - FreeRTOS OTA PAL for AmebaD (RTL8721D), make AWS=ON.
 *
 * Bridges the AWS IoT OTA Agent's OtaPalInterface_t to the in-tree slot writer
 * (ota_flash.c). The OTA *file* downloaded from AWS IS the build's OTA_All.bin
 * (32-byte Realtek OTA header + image2[+IMG3]); the agent streams it block-by-block
 * (otaconfigFILE_BLOCK_SIZE) and each block is fed straight to ota_flash_feed(),
 * which parses the header, erases + writes the inactive slot, holds the 8-byte boot
 * signature, and accumulates the firmware checksum. closeFile() runs ota_flash_finish()
 * (length + checksum verify, arms the new slot) and then verifies the AWS code-sign
 * signature over the downloaded bytes with mbedTLS.
 *
 * Block ordering: ota_flash_feed() is a *sequential* stream consumer. With
 * otaconfigMAX_NUM_BLOCKS_REQUEST small and AWS streaming the data plane in order,
 * blocks arrive sequentially; writeBlock() asserts offset == bytes-already-consumed
 * and rejects an out-of-order block rather than corrupting the slot. (This is a known
 * simplification - see the file-level note + the parent's design summary.)
 *
 * Signature verify (simplification to scrutinise on HW): the SHA-256 is computed
 * INCREMENTALLY over every byte fed to ota_flash_feed() - i.e. over the full
 * OTA_All.bin as downloaded (header + image). The AWS Signer / `create-stream`
 * job must therefore sign the SAME OTA_All.bin artifact end-to-end. The cert PEM is
 * read from pFileContext->pCertFilepath via newlib fopen over the LFS VFS.
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
static mbedtls_sha256_context s_sha;
static uint32_t           s_consumed;     /* bytes fed to ota_flash_feed so far (== expected next offset) */
static int                s_active;       /* a file is currently open */

/* Reset all PAL state. */
static void prvPalReset( void )
{
    memset( &s_slot, 0, sizeof( s_slot ) );
    mbedtls_sha256_free( &s_sha );
    s_consumed = 0;
    s_active = 0;
}

/* ----------------------------------------------------------------- create ---- */
static OtaPalStatus_t prvAbort( OtaFileContext_t * const pFileContext )
{
    ( void ) pFileContext;

    if( s_active )
    {
        OTA_PAL_LOG( "abort: discarding in-progress download (%u bytes)", ( unsigned ) s_consumed );
    }
    prvPalReset();

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

    if( ota_flash_begin( &s_slot ) != 0 )
    {
        OTA_PAL_LOG( "createFile: ota_flash_begin FAIL" );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    mbedtls_sha256_init( &s_sha );
    if( mbedtls_sha256_starts( &s_sha, 0 /* SHA-256, not 224 */ ) != 0 )
    {
        OTA_PAL_LOG( "createFile: sha256_starts FAIL" );
        return OTA_PAL_COMBINE_ERR( OtaPalRxFileCreateFailed, 0 );
    }

    s_consumed = 0;
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

    /* ota_flash_feed is a sequential consumer: only accept the next-in-order block. */
    if( offset != s_consumed )
    {
        OTA_PAL_LOG( "writeBlock: OUT-OF-ORDER offset=%u expected=%u (sequential PAL)",
                     ( unsigned ) offset, ( unsigned ) s_consumed );
        return -1;
    }

    if( ota_flash_feed( &s_slot, pData, blockSize ) != 0 )
    {
        OTA_PAL_LOG( "writeBlock: ota_flash_feed FAIL at offset=%u", ( unsigned ) offset );
        return -1;
    }

    /* Hash the raw downloaded bytes (full OTA_All.bin incl. 32B header). */
    ( void ) mbedtls_sha256_update( &s_sha, pData, blockSize );

    s_consumed += blockSize;

    /* The agent expects the number of bytes written on success. */
    return ( int16_t ) blockSize;
}

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

static OtaPalStatus_t prvVerifySignature( OtaFileContext_t * const pFileContext )
{
    int rc;
    mbedtls_x509_crt crt;
    uint8_t hash[ OTA_PAL_SHA256_LEN ];
    OtaPalStatus_t status = OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );

    if( ( pFileContext->pSignature == NULL ) || ( pFileContext->pSignature->size == 0U ) )
    {
        OTA_PAL_LOG( "verify: no signature in job" );
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
    }
    ( void ) pFileContext->pCertFilepath;   /* ignored: the code-sign cert is embedded above */

    /* Finalize the rolling hash over the downloaded image. */
    if( mbedtls_sha256_finish( &s_sha, hash ) != 0 )
    {
        OTA_PAL_LOG( "verify: sha256_finish FAIL" );
        return OTA_PAL_COMBINE_ERR( OtaPalSignatureCheckFailed, 0 );
    }

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

    if( !s_active )
    {
        OTA_PAL_LOG( "closeFile: no active file" );
        return OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
    }

    /* Verify the code-sign signature first (mbedTLS); finalizes the hash. */
    sigStatus = prvVerifySignature( pFileContext );
    if( OTA_PAL_MAIN_ERR( sigStatus ) != OtaPalSuccess )
    {
        /* Leave the slot un-armed: ota_flash_finish would write the boot sig last, so
         * skipping it means the bootloader will not pick this (un-finished) slot. */
        prvPalReset();
        if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
        return sigStatus;
    }

    /* Signature good: verify length+checksum and arm the new slot (writes boot sig last). */
    if( ota_flash_finish( &s_slot ) != 0 )
    {
        OTA_PAL_LOG( "closeFile: ota_flash_finish (len/checksum) FAIL" );
        prvPalReset();
        if( pFileContext != NULL ) { pFileContext->pFile = NULL; }
        return OTA_PAL_COMBINE_ERR( OtaPalFileClose, 0 );
    }

    OTA_PAL_LOG( "closeFile: image written + armed (%u bytes downloaded)", ( unsigned ) s_consumed );
    s_active = 0;
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
    OTA_PAL_LOG( "setPlatformImageState(%d) - no-op (no self-test)", ( int ) eState );
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

static OtaPalImageState_t prvGetPlatformImageState( OtaFileContext_t * const pFileContext )
{
    ( void ) pFileContext;
    /* Always report Valid: the running image is whatever the bootloader selected and
     * we do not implement pending-commit. */
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
