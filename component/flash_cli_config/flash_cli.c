/*
 * flash_cli.c - the cross-platform half of the firmware flash write-protect (board_flash_lock.h):
 * the lock/unlock/status helpers, the boot-time status print, and the shared `flash` console command.
 * The raw status-register read/write lives in each platform's board_flash_protect / _read_status.
 */
#include "board_flash_lock.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"        /* rxi log_info - the single project logger (both platforms) */

/* SR1 firmware-lock encodings (see board_flash_lock.h): BP/CMP bits, masked with SRP0|SEC|TB|BP. */
#define SR1_MASK       0xFCu
#define SR1_LOCK_FW    0x14u   /* TB=0, BP=101 (paired with SR2 CMP=1) */
#define SR1_LOCK_ALL   0x1Cu   /* TB=0, BP=111 */
#define SR2_CMP        0x40u

/* ---- shared helpers over the platform primitives -------------------------------------------- */

void board_flash_lock_fw( void )
{
    ( void ) board_flash_protect( BOARD_FLASH_LOCK_FW, NULL, NULL, NULL, NULL );
}

void board_flash_unlock( void )
{
    ( void ) board_flash_protect( BOARD_FLASH_UNLOCK, NULL, NULL, NULL, NULL );
}

int board_flash_is_fw_locked( void )
{
    uint8_t sr1 = 0, sr2 = 0;

    if( !board_flash_read_status( &sr1, &sr2 ) )
    {
        return 0;
    }
    /* firmware region locked = fw-lock (SR1=0x14 + SR2 CMP) or whole-chip lock (SR1=0x1C). */
    if( ( ( sr1 & SR1_MASK ) == SR1_LOCK_FW ) && ( ( sr2 & SR2_CMP ) != 0 ) )
    {
        return 1;
    }
    return ( ( sr1 & SR1_MASK ) == SR1_LOCK_ALL );
}

void board_flash_log_status( void )
{
    uint8_t sr1 = 0, sr2 = 0;

    if( !board_flash_read_status( &sr1, &sr2 ) )
    {
        log_info( "[flash] status unavailable" );
        return;
    }
    log_info( "[flash] firmware region %s (SR1=0x%02x SR2=0x%02x)",
              board_flash_is_fw_locked() ? "LOCKED" : "unlocked", sr1, sr2 );
}

/* ---- the shared `flash` console command ----------------------------------------------------- */

static BaseType_t cmd_flash_cli( char * out, size_t outlen, const char * cmd )
{
    const char * sub;
    BaseType_t   sublen = 0;

    sub = FreeRTOS_CLIGetParameter( cmd, 1, &sublen );
    if( sub == NULL )
    {
        snprintf( out, outlen, "usage: flash <lock [all] | unlock | status>\r\n" );
        return pdFALSE;
    }

    if( sublen == 4 && strncmp( sub, "lock", 4 ) == 0 )
    {
        const char *       arg = FreeRTOS_CLIGetParameter( cmd, 2, &sublen );
        int                all = ( arg != NULL && sublen == 3 && strncmp( arg, "all", 3 ) == 0 );
        uint8_t            s1b = 0, s1a = 0, s2b = 0, s2a = 0;
        int                ok  = board_flash_protect( all ? BOARD_FLASH_LOCK_ALL : BOARD_FLASH_LOCK_FW,
                                                      &s1b, &s1a, &s2b, &s2a );
        snprintf( out, outlen, "flash lock %s: SR1 %02x->%02x SR2 %02x->%02x %s\r\n",
                  all ? "all" : "fw", s1b, s1a, s2b, s2a, ok ? "OK" : "FAIL" );
        return pdFALSE;
    }
    if( sublen == 6 && strncmp( sub, "unlock", 6 ) == 0 )
    {
        uint8_t s1b = 0, s1a = 0, s2b = 0, s2a = 0;
        int     ok  = board_flash_protect( BOARD_FLASH_UNLOCK, &s1b, &s1a, &s2b, &s2a );
        snprintf( out, outlen, "flash unlock: SR1 %02x->%02x SR2 %02x->%02x %s\r\n",
                  s1b, s1a, s2b, s2a, ok ? "OK" : "FAIL" );
        return pdFALSE;
    }
    if( sublen == 6 && strncmp( sub, "status", 6 ) == 0 &&
        FreeRTOS_CLIGetParameter( cmd, 2, &sublen ) == NULL )   /* bare `flash status` = lock state */
    {
        uint8_t sr1 = 0, sr2 = 0;
        if( board_flash_read_status( &sr1, &sr2 ) )
        {
            snprintf( out, outlen, "flash: firmware region %s (SR1=0x%02x SR2=0x%02x)\r\n",
                      board_flash_is_fw_locked() ? "LOCKED" : "unlocked", sr1, sr2 );
        }
        else
        {
            snprintf( out, outlen, "flash: status read failed\r\n" );
        }
        return pdFALSE;
    }

    /* AmebaD's raw erase/read/write/status/cache debug lives under a separate `flashdbg` command. */
    snprintf( out, outlen, "usage: flash <lock [all] | unlock | status>\r\n" );
    return pdFALSE;
}

void flash_cli_register( void )
{
    static const CLI_Command_Definition_t def =
    {
        "flash",
        "flash <lock [all]|unlock|status>:\r\n SPI-NOR firmware write-protect (bottom 3 MB); status shows the lock state\r\n",
        cmd_flash_cli,
        -1
    };
    FreeRTOS_CLIRegisterCommand( &def );
}
