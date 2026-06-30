/*
 * time_cli.c — wall-clock / SNTP console commands for the AmebaD KM4 FreeRTOS-Plus-CLI:
 *   date          print the current local date/time (and whether it is SNTP-synced)
 *   uptime        print time since boot
 *   sntp_update   force an SNTP sync NOW; the server is the NTP_SERVER env var
 *
 * Registered from main() after amebad_monitor_start(), like aws_cli.c / fs_cli.c — no edit
 * to the shared monitor. `date`/`uptime` work in every KM4 image (time_base.c is always
 * linked). `sntp_update` calls sntp_sync_once(): the real implementation lives in
 * sntp_time.c (make SNTP=ON); the weak stub below keeps non-SNTP images linking and reports
 * the feature is absent.
 */
#include <stdio.h>
#include <stdlib.h>          /* getenv */
#include <time.h>            /* time, localtime_r, strftime, struct tm */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"

#include "syscalls.h"        /* amebad_time_is_synced */

/* Force one SNTP round and set the wall clock. STRONG def in sntp_time.c overrides this
 * WEAK stub when the SNTP client is built (make SNTP=ON). -100 => "not built". */
int __attribute__((weak)) sntp_sync_once(void)
{
    return -100;
}

/* --------------------------------------------------------------------- date --- */
static BaseType_t cmd_date(char *out, size_t outlen, const char *cmd)
{
    time_t    t = time(NULL);
    struct tm lt;
    char      buf[48];

    (void)cmd;
    localtime_r(&t, &lt);                 /* honors TZ (main.c sets CST-8 + tzset) */
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", &lt);
    snprintf(out, outlen, "%s (%s)\r\n", buf,
             amebad_time_is_synced() ? "synced" : "not synced, uptime-based");
    return pdFALSE;
}

/* ------------------------------------------------------------------- uptime --- */
static BaseType_t cmd_uptime(char *out, size_t outlen, const char *cmd)
{
    /* TickType_t is 32-bit at configTICK_RATE_HZ=1000 -> wraps after ~49.7 days. */
    unsigned long ms  = (unsigned long)xTaskGetTickCount() * (unsigned long)portTICK_PERIOD_MS;
    unsigned long s   = ms / 1000UL;
    unsigned long d   = s / 86400UL;
    unsigned long h   = (s % 86400UL) / 3600UL;
    unsigned long m   = (s % 3600UL) / 60UL;
    unsigned long sec = s % 60UL;

    (void)cmd;
    snprintf(out, outlen, "up %lud %02lu:%02lu:%02lu  (%lu ms since boot)\r\n", d, h, m, sec, ms);
    return pdFALSE;
}

/* -------------------------------------------------------------- sntp_update --- */
static BaseType_t cmd_sntp_update(char *out, size_t outlen, const char *cmd)
{
    const char *srv;
    int rc;

    (void)cmd;
    rc = sntp_sync_once();
    if (rc == -100) {
        snprintf(out, outlen, "sntp_update: SNTP client not built in this image (make SNTP=ON)\r\n");
        return pdFALSE;
    }
    srv = getenv("NTP_SERVER");
    snprintf(out, outlen, "sntp_update: server=%s -> %s (rc=%d)\r\n",
             (srv && srv[0]) ? srv : "ntp.aliyun.com",
             (rc == 0) ? "OK" : "FAILED", rc);
    return pdFALSE;
}

/* ----------------------------------------------------------- registration --- */
void time_cli_register(void)
{
    static const CLI_Command_Definition_t defs[] = {
        { "date",        "date:\r\n print the current local date/time\r\n",                     cmd_date,        0 },
        { "uptime",      "uptime:\r\n print time since boot\r\n",                               cmd_uptime,      0 },
        { "sntp_update", "sntp_update:\r\n force an SNTP sync now (server from NTP_SERVER)\r\n", cmd_sntp_update, 0 },
    };
    unsigned i;

    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); i++) {
        FreeRTOS_CLIRegisterCommand(&defs[i]);
    }
}
