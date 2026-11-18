/*
 * bench_cli.c — the `bench` console command running the upstream Mbed TLS microbenchmark
 * (mbedtls_bench(), [MCB] scores) for the shared FreeRTOS-Plus-CLI console. On-demand
 * alternative to AmebaD's auto-run from main.c under AT_PROJECT_CRYPTOBENCH (make BENCH=ON).
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "mbedtls_bench.h"

static BaseType_t cmd_bench( char *out, size_t outlen, const char *cmd )
{
    (void) cmd;
    /* Runs synchronously on the CLI task (several seconds); scores stream out over at_log. */
    mbedtls_bench();
    snprintf( out, outlen, "bench: done (see [MCB] lines above)\r\n" );
    return pdFALSE;
}

static const CLI_Command_Definition_t kBenchCmd = {
    "bench",
    "bench:\r\n run the Mbed TLS crypto microbenchmark ([MCB] cyc/B + KB/s + op/s)\r\n",
    cmd_bench,
    0
};

void bench_cli_register( void )
{
    FreeRTOS_CLIRegisterCommand( &kBenchCmd );
}
