/*
 * iperf_cli.c — an `iperf` console command driving the upstream iperf3 library
 * (component/iperf3_config) over the FreeRTOS-Plus-TCP BSD-sockets shim
 * (component/syscall_config/bsd_socket.c + pthread_shim.c). Replaces the old
 * homegrown iperf2 probes. Built only with make IPERF=ON (AMEBAD_ENABLE_IPERF).
 *
 *   iperf -s [port]                       run a server (default port 5201)
 *   iperf -c <host> [port] [secs] [mbit]  run a client to <host>
 *
 * The optional [mbit] caps the client's offered bitrate (application-level pacing).
 *
 * KNOWN LIMITATION (device-as-client / TX): a sustained upload wedges the whole chip after ~4 s of
 * continuous TX — a lib_wlan.a/firmware-level hang (the console dies, the TCP connection goes
 * half-open). It reproduces identically on the stock Realtek SDK's own ATWT iperf2 client, so it is
 * NOT specific to this FreeRTOS-Plus-TCP port or to the skb pool depth/rate (2 / 8 / 20 Mbit/s all
 * hang at ~4 s — it is time-, not byte-driven). Use `iperf -s` (device-as-server / RX) for sustained
 * tests; the client direction is reliable only for a short (<~3 s) burst. See the iperf3 component memo.
 *
 * iperf_run_client/server block the CLI task for the test, streaming results to the
 * console via the library's printf path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>   /* mkdir — iperf3 stages its per-stream buffer in /tmp (mkstemp) */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "iperf_api.h"

static int arg(const char *cmd, UBaseType_t idx, char *buf, size_t sz)
{
    BaseType_t l = 0;
    const char *p = FreeRTOS_CLIGetParameter(cmd, idx, &l);
    if (p == NULL || l <= 0) { return -1; }
    if ((size_t)l >= sz) { l = (BaseType_t)(sz - 1); }
    memcpy(buf, p, (size_t)l);
    buf[l] = '\0';
    return (int)l;
}

static BaseType_t cmd_iperf(char *out, size_t outlen, const char *cmd)
{
    char mode[8], a2[64], a3[16], a4[16], a5[16];
    struct iperf_test *t;
    int rc;

    out[0] = '\0';
    (void)mkdir("/tmp", 0777);   /* iperf3 mkstemp()s its mmap'd buffer here (our mmap is malloc-backed) */
    if (arg(cmd, 1, mode, sizeof mode) <= 0) {
        snprintf(out, outlen, "usage: iperf -s [port] | iperf -c <host> [port] [secs]\r\n");
        return pdFALSE;
    }

    t = iperf_new_test();
    if (t == NULL) { snprintf(out, outlen, "iperf: out of memory\r\n"); return pdFALSE; }
    iperf_defaults(t);
    /* iperf's per-stream read/write buffer is mmap'd (= malloc'd here) at blksize; the 128 KB default
     * no longer fits the FreeRTOS heap once the WiFi skb pool is enlarged (rtw_opt_skbuf.c). 16 KB is
     * ample for the read/write granularity at this link rate. */
    iperf_set_test_blksize(t, 16 * 1024);

    if (strcmp(mode, "-s") == 0) {
        iperf_set_test_role(t, 's');
        if (arg(cmd, 2, a2, sizeof a2) > 0) { iperf_set_test_server_port(t, atoi(a2)); }
        rc = iperf_run_server(t);
    } else if (strcmp(mode, "-c") == 0) {
        if (arg(cmd, 2, a2, sizeof a2) <= 0) {
            snprintf(out, outlen, "usage: iperf -c <host> [port] [secs] [mbit]\r\n");
            iperf_free_test(t);
            return pdFALSE;
        }
        iperf_set_test_role(t, 'c');
        iperf_set_test_server_hostname(t, a2);
        if (arg(cmd, 3, a3, sizeof a3) > 0) { iperf_set_test_server_port(t, atoi(a3)); }
        if (arg(cmd, 4, a4, sizeof a4) > 0) { iperf_set_test_duration(t, atoi(a4)); }
        if (arg(cmd, 5, a5, sizeof a5) > 0) {
            iperf_set_test_rate(t, (uint64_t)atoi(a5) * 1000000U);   /* Mbit/s -> bit/s pacing */
        }
        rc = iperf_run_client(t);
    } else {
        snprintf(out, outlen, "usage: iperf -s [port] | iperf -c <host> [port] [secs]\r\n");
        iperf_free_test(t);
        return pdFALSE;
    }

    snprintf(out, outlen, "iperf: %s (rc=%d)\r\n", (rc == 0) ? "done" : "error", rc);
    iperf_free_test(t);
    return pdFALSE;
}

void iperf_cli_register(void)
{
    static const CLI_Command_Definition_t def = {
        "iperf",
        "iperf -s [port] | -c <host> [port] [secs] [mbit]:\r\n run the iperf3 server/client\r\n",
        cmd_iperf, -1
    };
    FreeRTOS_CLIRegisterCommand(&def);
}
