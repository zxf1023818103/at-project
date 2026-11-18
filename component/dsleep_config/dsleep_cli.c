/*
 * dsleep_cli.c - the cross-platform half of the `dsleep [uart] [rtc <seconds>]` command: parse the
 * wake sources, then call the platform primitive board_dsleep_enter() (see dsleep.h). The chip-
 * specific deep-sleep + wake-source sequence lives in each platform's board_dsleep_port.
 */
#include "dsleep.h"

#include <stdio.h>
#include <stdlib.h>   /* atoi */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

/* `dsleep [uart] [rtc <seconds>]` - options order-independent, both optional but at least one
 * required. `uart` = wake on a console/UART-RX edge; `rtc <n>` = wake after n seconds; both = wake on
 * whichever fires first. bl616/bl602 reboot on wake (this never returns on success); AmebaD resumes. */
static BaseType_t cmd_dsleep( char *out, size_t outlen, const char *cmd )
{
    bool     uart = false;
    uint32_t rtc  = 0;

    for ( int i = 1;; i++ )
    {
        BaseType_t   plen = 0;
        const char * p    = FreeRTOS_CLIGetParameter( cmd, i, &plen );
        if ( p == NULL )
        {
            break;
        }
        if ( plen == 4 && strncmp( p, "uart", 4 ) == 0 )
        {
            uart = true;
        }
        else if ( plen == 3 && strncmp( p, "rtc", 3 ) == 0 )
        {
            BaseType_t   vlen = 0;
            const char * v    = FreeRTOS_CLIGetParameter( cmd, i + 1, &vlen );
            if ( v != NULL )
            {
                rtc = ( uint32_t ) atoi( v );   /* atoi stops at the space after the token */
                i++;
            }
        }
    }

    if ( !uart && rtc == 0 )
    {
        snprintf( out, outlen, "usage: dsleep [uart] [rtc <seconds>]  (at least one wake source)\r\n" );
        return pdFALSE;
    }

    printf( "[DS] entering low-power sleep: uart=%d rtc=%us ...\r\n", uart ? 1 : 0, ( unsigned ) rtc );
    fflush( stdout );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );   /* let the console UART FIFO drain before the core powers down */

    int r = board_dsleep_enter( uart, rtc );

    /* Only reached on AmebaD (resume-in-place) or on an entry error; bl616/bl602 reboot on wake. */
    if ( r < 0 )
    {
        snprintf( out, outlen, "dsleep: entry failed / unsupported on this chip (%d)\r\n", r );
    }
    else
    {
        snprintf( out, outlen, "[DS] resumed from low-power sleep\r\n" );
    }
    return pdFALSE;
}

void dsleep_cli_register( void )
{
    static const CLI_Command_Definition_t def =
    {
        "dsleep",
        "dsleep [uart] [rtc <seconds>]:\r\n low-power sleep, wake on UART-RX and/or RTC (bl616/bl602 reboot on wake; amebad resumes)\r\n",
        cmd_dsleep,
        -1
    };
    FreeRTOS_CLIRegisterCommand( &def );
}
