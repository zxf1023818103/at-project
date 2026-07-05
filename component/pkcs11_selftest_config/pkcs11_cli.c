/*
 * pkcs11_cli.c — the `pkcs11` console command running the corePKCS11 token self-test
 * (pkcs11_selftest_run(), [PK11] scores) for the shared FreeRTOS-Plus-CLI console. On-demand
 * alternative to AmebaD's auto-run from main.c under AT_PROJECT_PKCS11 (make PKCS11=ON).
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "pkcs11_selftest.h"

static BaseType_t cmd_pkcs11( char *out, size_t outlen, const char *cmd )
{
    (void) cmd;
    /* Runs synchronously on the CLI task (EC P-256 keygen + sign/verify); result over at_log. */
    pkcs11_selftest_run();
    snprintf( out, outlen, "pkcs11: done (see [PK11] lines above)\r\n" );
    return pdFALSE;
}

static const CLI_Command_Definition_t kPkcs11Cmd = {
    "pkcs11",
    "pkcs11:\r\n run the corePKCS11 token self-test (keygen -> sign -> verify, LittleFS-persisted)\r\n",
    cmd_pkcs11,
    0
};

void pkcs11_cli_register( void )
{
    FreeRTOS_CLIRegisterCommand( &kPkcs11Cmd );
}
