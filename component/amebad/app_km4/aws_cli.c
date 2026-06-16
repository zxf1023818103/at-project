/*
 * aws_cli.c - `set` / `blemac` console commands for AWS IoT provisioning (AWS=ON).
 *
 * Registered into the running FreeRTOS-Plus-CLI list (the same one the monitor uses)
 * from main() after amebad_monitor_start(), so no edit to the shared monitor_cli.c.
 *
 *   blemac                 print the module BLE MAC (efuse 0x190) = AWS Thing name
 *   set endpoint <ep>      store the AWS IoT ATS endpoint   -> env var "endpoint" (KV/endpoint)
 *   set cert <b64>         client-cert base64 body          -> plain LittleFS DER + env "cert"
 *   set key  <b64>         client-key  base64 body          -> TF-M ITS (secret)
 *   setenv <name> <value>  set an env var (empty value unsets); auto-persisted to KV/<name>
 *   getenv <name>          print an env var
 *   env                    list all env vars
 *
 * The CLI line editor caps a command line at 80 bytes (monitor_cli.c CLI_MAX_INPUT),
 * far short of a ~1.7 KB PEM. So `set cert` / `set key` switch to RAW streaming: the
 * handler reads bytes straight off the LOGUART (it runs in the console task's context,
 * the only UART reader) and accumulates lines until a line containing only "EOF".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "ameba_soc.h"          /* LOGUART_Readable / LOGUART_GetChar / DiagPutChar / DiagPrintf */

#include "psa/protected_storage.h"
#include "psa/internal_trusted_storage.h"
#include "aws_provision.h"
#include "section_config.h"     /* IMAGE2_RAM_TEXT_SECTION (place the SPIC probe in RAM) */

/* `set cert`/`set key` take the credential's base64 BODY as one ordinary CLI argument (the
 * standard FreeRTOS+CLI path - FreeRTOS_CLIGetParameter, no raw LOGUART streaming). The host strips
 * the PEM armor (the "-----BEGIN ... PRIVATE KEY-----" lines contain spaces, which the CLI splits
 * on) and all newlines, so the body is a single space-free token; the device base64-decodes it to
 * DER. This needs the CLI line buffer big enough for a ~1.6 KB base64 body: monitor_cli.c raises
 * CLI_MAX_INPUT for that. */

/* ----------------------------------------------------------------- blemac --- */
static BaseType_t cmd_blemac(char *out, size_t outlen, const char *cmd)
{
    char mac[16];
    (void)cmd;
    if (aws_get_ble_mac(mac, sizeof(mac)) == 0)
        snprintf(out, outlen, "BLE MAC (Thing name): %s\r\n", mac);
    else
        snprintf(out, outlen, "blemac: efuse read failed\r\n");
    return pdFALSE;
}

/* -------------------------------------------------------------------- set --- */
static BaseType_t cmd_set(char *out, size_t outlen, const char *cmd)
{
    BaseType_t l1 = 0, l2 = 0;
    const char *sub = FreeRTOS_CLIGetParameter(cmd, 1, &l1);
    const char *val = FreeRTOS_CLIGetParameter(cmd, 2, &l2);

    out[0] = '\0';
    if (sub == NULL) {
        snprintf(out, outlen, "usage: set <endpoint <ep>|cert|key>\r\n");
        return pdFALSE;
    }

    if (l1 == 8 && strncmp(sub, "endpoint", 8) == 0) {
        char ep[AWS_ENDPOINT_MAX];
        if (val == NULL || l2 == 0 || (size_t)l2 >= sizeof(ep)) {
            snprintf(out, outlen, "usage: set endpoint <ats-endpoint>\r\n");
            return pdFALSE;
        }
        memcpy(ep, val, (size_t)l2);
        ep[l2] = '\0';
        /* endpoint is not secret -> env var "endpoint" (auto-persisted to KV/endpoint by the
         * --wrap=setenv hook). */
        snprintf(out, outlen, "set endpoint %s -> env %s\r\n",
                 ep, (setenv("endpoint", ep, 1) == 0) ? "OK" : "FAIL");
        return pdFALSE;
    }

    if ((l1 == 4 && strncmp(sub, "cert", 4) == 0) ||
        (l1 == 3 && strncmp(sub, "key", 3) == 0)) {
        int is_key = (sub[0] == 'k');
        int rc;
        /* val/l2 = parameter 2 = the credential's base64 body (one space-free CLI token; the host
         * stripped the armor + newlines). Standard FreeRTOS+CLI - no raw LOGUART streaming. */
        if (val == NULL || l2 == 0) {
            snprintf(out, outlen, "usage: set %s <base64-body>\r\n", is_key ? "key" : "cert");
            return pdFALSE;
        }
        /* key -> TF-M ITS (secret); cert -> plain LittleFS DER file + env "cert" (path). */
        rc = is_key ? aws_store_key(val, (size_t)l2) : aws_store_cert_file(val, (size_t)l2);
        snprintf(out, outlen, "set %s: %d b64 -> %s\r\n",
                 is_key ? "key" : "cert", (int)l2, (rc == 0) ? "OK" : "FAIL");
        return pdFALSE;
    }

    snprintf(out, outlen, "set: unknown '%.*s' (endpoint|cert|key)\r\n", (int)l1, sub);
    return pdFALSE;
}

/* ------------------------------------------------------- setenv/getenv/env --- *
 * Generic environment-variable console commands over newlib's environ. setenv is
 * --wrap'd (app_km4 CMakeLists) so a `setenv` here is auto-persisted to KV/<name>
 * and reloaded on the next boot (syscall_config/env.c). Names/values are single
 * space-free CLI tokens (FreeRTOS_CLIGetParameter splits on spaces). */
#define ENV_CLI_NAME_MAX   64
#define ENV_CLI_VALUE_MAX  256

static BaseType_t cmd_setenv(char *out, size_t outlen, const char *cmd)
{
    BaseType_t ln = 0, lv = 0;
    const char *name = FreeRTOS_CLIGetParameter(cmd, 1, &ln);
    const char *val  = FreeRTOS_CLIGetParameter(cmd, 2, &lv);
    char nbuf[ENV_CLI_NAME_MAX], vbuf[ENV_CLI_VALUE_MAX];

    if (name == NULL || ln == 0 || (size_t)ln >= sizeof(nbuf)) {
        snprintf(out, outlen, "usage: setenv <name> <value>  (empty value unsets)\r\n");
        return pdFALSE;
    }
    memcpy(nbuf, name, (size_t)ln);
    nbuf[ln] = '\0';
    if (val == NULL || lv == 0) {                 /* no value -> unset */
        unsetenv(nbuf);
        snprintf(out, outlen, "unset %s\r\n", nbuf);
        return pdFALSE;
    }
    if ((size_t)lv >= sizeof(vbuf)) {
        snprintf(out, outlen, "setenv: value too long (max %u)\r\n",
                 (unsigned)(sizeof(vbuf) - 1));
        return pdFALSE;
    }
    memcpy(vbuf, val, (size_t)lv);
    vbuf[lv] = '\0';
    snprintf(out, outlen, "setenv %s=%s -> %s\r\n", nbuf, vbuf,
             (setenv(nbuf, vbuf, 1) == 0) ? "OK" : "FAIL");
    return pdFALSE;
}

static BaseType_t cmd_getenv(char *out, size_t outlen, const char *cmd)
{
    BaseType_t ln = 0;
    const char *name = FreeRTOS_CLIGetParameter(cmd, 1, &ln);
    const char *v;
    char nbuf[ENV_CLI_NAME_MAX];

    if (name == NULL || ln == 0 || (size_t)ln >= sizeof(nbuf)) {
        snprintf(out, outlen, "usage: getenv <name>\r\n");
        return pdFALSE;
    }
    memcpy(nbuf, name, (size_t)ln);
    nbuf[ln] = '\0';
    v = getenv(nbuf);
    snprintf(out, outlen, "%s=%s\r\n", nbuf, v ? v : "");
    return pdFALSE;
}

/* List environ one entry per FreeRTOS+CLI call (return pdTRUE => call again), so a long
 * environment isn't truncated by the single CLI output buffer. */
static BaseType_t cmd_env(char *out, size_t outlen, const char *cmd)
{
    extern char **environ;
    static int idx = 0;
    (void)cmd;

    if (environ == NULL || environ[idx] == NULL) {
        idx = 0;
        out[0] = '\0';
        return pdFALSE;                 /* nothing (more) to print */
    }
    snprintf(out, outlen, "%s\r\n", environ[idx]);
    idx++;
    if (environ[idx] == NULL) {
        idx = 0;
        return pdFALSE;                 /* that was the last entry */
    }
    return pdTRUE;                       /* call again for the next entry */
}

/* --------------------------------------------------------------- pstest --- */
/* Diagnostic: write <size> bytes to TF-M Protected Storage at runtime, retrying up to
 * <retries> times, and report whether/when it succeeds + status + elapsed ms. This probes
 * the runtime PS-write blocker (SPE flash program vs KM0 XIP contention): does a 32 B write
 * succeed (experiment 2), and does retrying let a large write through (experiment 1, NS-layer
 * retry - no SPE change needed if it works)?  Usage: `pstest [size] [retries]`. */
static long parse_arg(const char *cmd, UBaseType_t idx, long dflt)
{
    BaseType_t l = 0;
    const char *p = FreeRTOS_CLIGetParameter(cmd, idx, &l);
    if (!p || l <= 0) return dflt;
    char tmp[16];
    if ((size_t)l >= sizeof(tmp)) l = sizeof(tmp) - 1;
    memcpy(tmp, p, (size_t)l);
    tmp[l] = '\0';
    return strtol(tmp, NULL, 0);
}

static BaseType_t cmd_pstest(char *out, size_t outlen, const char *cmd)
{
    static uint8_t buf[4096];
    long sz   = parse_arg(cmd, 1, 256);
    long tries = parse_arg(cmd, 2, 1);
    long use_its = parse_arg(cmd, 3, 0);   /* arg3=1 -> ITS (the real aws_store_key path); else PS */
    long uidsel  = parse_arg(cmd, 4, 0);   /* arg4: 0=pstest uid  1=AWS_KEY_UID  2=low test uid */
    if (sz < 1)    sz = 1;
    if (sz > (long)sizeof(buf)) sz = (long)sizeof(buf);
    if (tries < 1) tries = 1;
    if (tries > 50) tries = 50;

    for (long i = 0; i < sz; i++)
        buf[i] = (uint8_t)(i & 0xff);

    psa_storage_uid_t uid = (psa_storage_uid_t)0x70737401ULL;          /* "pst\1" (works) */
    if (uidsel == 1) uid = (psa_storage_uid_t)0x4157535F4B455901ULL;   /* == AWS_KEY_UID */
    else if (uidsel == 2) uid = (psa_storage_uid_t)0x70770003ULL;      /* low value */
    psa_status_t s = PSA_ERROR_GENERIC_ERROR;
    long got = 0;
    TickType_t t0 = xTaskGetTickCount();
    for (long t = 1; t <= tries; t++) {
        s = use_its ? psa_its_set(uid, (size_t)sz, buf, 0)
                    : psa_ps_set(uid, (size_t)sz, buf, 0);
        got = t;
        if (s == PSA_SUCCESS)
            break;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    TickType_t dt = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
    if (use_its) (void)psa_its_remove(uid); else (void)psa_ps_remove(uid);

    snprintf(out, outlen, "pstest %ldB x%ld %s -> %s on try %ld (status=%d, %u ms)\r\n",
             sz, tries, use_its ? "ITS" : "PS", (s == PSA_SUCCESS) ? "OK" : "FAIL",
             got, (int)s, (unsigned)dt);
    return pdFALSE;
}

/* --------------------------------------------------------------- xiptest --- */
/* SPIC-corruption probe (AN0400 §18.2: code that operates/observes the SPIC must run from RAM,
 * not XIP). FNV-hash the NS XIP code region as DATA before and after a runtime psa_its_set, plus
 * sample a 256 B window around the address that faulted earlier (wifi_connect, 0x0E0096xx).
 * CHANGED => the runtime secure flash write corrupts the NS XIP read path (the SPIC returns
 * different content) - proves it's a SPIC/XIP problem, not generic RAM corruption. The probe runs
 * from RAM so it stays alive even if XIP execution is unreliable after the write; output goes via
 * DiagPrintf (ROM), not snprintf (XIP). The SPE Driver_Flash.c Cache_Flush already invalidated the
 * cache, so the 'after' read comes fresh from the SPIC, not a stale cache line. */
IMAGE2_RAM_TEXT_SECTION
static uint32_t xip_csum(uint32_t base, uint32_t len, uint8_t *win, uint32_t winoff, uint32_t winlen)
{
    const volatile uint8_t *p = (const volatile uint8_t *)base;
    uint32_t s = 0x811c9dc5u;   /* FNV-1a */
    uint32_t i;
    for (i = 0; i < len; i++) s = (s ^ p[i]) * 16777619u;
    p = (const volatile uint8_t *)(base + winoff);
    for (i = 0; i < winlen; i++) win[i] = p[i];
    return s;
}

IMAGE2_RAM_TEXT_SECTION
static BaseType_t cmd_xiptest(char *out, size_t outlen, const char *cmd)
{
    static uint8_t buf[256];
    static uint8_t w0[256], w1[256];
    const uint32_t base = 0x0E004000u, len = 0x3C000u;   /* ~240 KB of NS XIP code */
    const uint32_t winoff = 0x5600u;                     /* -> 0x0E009600 (wifi_connect region) */
    const psa_storage_uid_t uid = (psa_storage_uid_t)0x5849500001ULL;  /* "XIP\0\1" */
    uint32_t c0, c1;
    psa_status_t s;
    int i, diff = -1;
    (void)cmd; (void)outlen;

    /* Stash the verdict in reserved NS RAM (0x10002020, RSVD_RAM_NS) with sentinels so it
     * survives a post-write wedge and can be read via OpenOCD (serial is unreliable then). */
    volatile uint32_t *r = (volatile uint32_t *)0x10002020u;
    r[0] = 0xC0DE0000u; r[7] = 0u; __DSB();

    for (i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    c0 = xip_csum(base, len, w0, winoff, 256);
    r[1] = c0; __DSB();
    DiagPrintf("[XIP] c0=%08x, writing...\n\r", (unsigned)c0);
    s  = psa_its_set(uid, 256, buf, PSA_STORAGE_FLAG_NONE);
    r[2] = (uint32_t)s; __DSB();
    DiagPrintf("[XIP] set=%d, re-reading...\n\r", (int)s);
    c1 = xip_csum(base, len, w1, winoff, 256);
    for (i = 0; i < 256; i++) if (w0[i] != w1[i]) { diff = i; break; }
    r[3] = c1;
    r[4] = (c0 == c1) ? 0x5A4D5A4Du : 0xBAD0BAD0u;       /* SAME / CHANGED */
    r[5] = (uint32_t)diff;
    r[6] = (diff >= 0) ? (((uint32_t)w0[diff] << 8) | w1[diff]) : 0xFFFFFFFFu;
    r[7] = 0xC0DEFFFFu; __DSB();                          /* end sentinel = complete */
    /* Print the verdict NOW (DiagPrintf is ROM) - before any further XIP call (psa_its_remove). */
    DiagPrintf("[XIP] c1=%08x %s\n\r", (unsigned)c1, (c0 == c1) ? "SAME" : "CHANGED");
    if (diff >= 0)
        DiagPrintf("[XIP] win 0x%08x +%d: %02x -> %02x\n\r",
                   (unsigned)(base + winoff), diff, w0[diff], w1[diff]);
    (void)psa_its_remove(uid);
    DiagPrintf("[XIP] done (survived)\n\r");
    out[0] = '\0';
    return pdFALSE;
}

/* ----------------------------------------------------------- registration --- */
void aws_cli_register(void)
{
    static const CLI_Command_Definition_t cmd_xiptest_def = {
        "xiptest", "xiptest:\r\n RAM-resident: checksum NS XIP before/after a runtime ITS write\r\n",
        cmd_xiptest, 0
    };
    FreeRTOS_CLIRegisterCommand(&cmd_xiptest_def);
    static const CLI_Command_Definition_t cmd_blemac_def = {
        "blemac", "blemac:\r\n print the module BLE MAC (AWS Thing name)\r\n",
        cmd_blemac, 0
    };
    static const CLI_Command_Definition_t cmd_set_def = {
        "set", "set <endpoint <ep>|cert <b64>|key <b64>>:\r\n provision AWS creds (endpoint->env, cert->LFS file, key->TF-M ITS)\r\n",
        cmd_set, -1
    };
    static const CLI_Command_Definition_t cmd_setenv_def = {
        "setenv", "setenv <name> <value>:\r\n set an env var (empty value unsets); persisted to KV/<name>\r\n",
        cmd_setenv, -1
    };
    static const CLI_Command_Definition_t cmd_getenv_def = {
        "getenv", "getenv <name>:\r\n print an env var\r\n",
        cmd_getenv, 1
    };
    static const CLI_Command_Definition_t cmd_env_def = {
        "env", "env:\r\n list all env vars\r\n",
        cmd_env, 0
    };
    static const CLI_Command_Definition_t cmd_pstest_def = {
        "pstest", "pstest [size] [retries]:\r\n diagnostic: runtime PS write of <size> bytes, retried\r\n",
        cmd_pstest, -1
    };
    FreeRTOS_CLIRegisterCommand(&cmd_blemac_def);
    FreeRTOS_CLIRegisterCommand(&cmd_set_def);
    FreeRTOS_CLIRegisterCommand(&cmd_setenv_def);
    FreeRTOS_CLIRegisterCommand(&cmd_getenv_def);
    FreeRTOS_CLIRegisterCommand(&cmd_env_def);
    FreeRTOS_CLIRegisterCommand(&cmd_pstest_def);
}
