/*
 * aws_platform.c - AmebaD platform glue for the shared AWS component (component/aws_iot_config).
 *
 *   - aws_get_mac()           : Thing name / MQTT client id = the module BLE MAC (efuse 0x190).
 *   - aws_cli_platform()      : the AmebaD-specific `aws` sub-verbs `blemac` and `secchk`.
 *   - aws_platform_diag_register() : the TF-M PSA diagnostics `pstest` / `xiptest`, registered as
 *                               separate top-level commands (TF-M builds only).
 *
 * The shared aws_provision.c / aws_cli.c handle endpoint/cert/key/ca provisioning + the `aws` command;
 * the client-key backend is aws_secret_store_its.c (TF-M ITS) or aws_secret_store_file.c (non-TF-M).
 * (This file replaces the former project-local aws_provision.c + aws_cli.c.)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "ameba_soc.h"          /* HAL_READ32 / efuse regs / LOGUART */

#ifdef AMEBAD_TFM_STORAGE
#include "psa/protected_storage.h"
#include "psa/internal_trusted_storage.h"
#include "section_config.h"     /* IMAGE2_RAM_TEXT_SECTION (RAM-resident SPIC probe) */
#endif

#include "aws_iot.h"

/* aws_get_mac moved to the shared component/aws_iot_config/aws_mac.c (BT MAC via
 * at_netif_bt_mac -> efuse 0x190 here, now device-mutex-guarded in realtek_netif.c) —
 * one Thing-name identity on every platform. */

/* ------------------------------------------------ `aws` platform sub-verbs ---- */
/* Called by the shared aws_cli.c for an `aws <sub>` verb it doesn't handle. AmebaD adds `blemac`
 * (Thing name) and `secchk` (RSIP/Secure-Boot enable state). Return 0 if handled, -1 if unknown. */
int aws_cli_platform(const char *sub, size_t sublen, const char *arg, size_t arglen,
                     char *out, size_t outlen)
{
    (void) arg; (void) arglen;

    if (sublen == 6 && strncmp(sub, "blemac", 6) == 0) {
        char mac[16];
        if (aws_get_mac(mac, sizeof(mac)) == 0)
            snprintf(out, outlen, "BLE MAC (Thing name): %s\r\n", mac);
        else
            snprintf(out, outlen, "blemac: efuse read failed\r\n");
        return 0;
    }

    /* `secchk`: report RSIP flash-encryption + Secure Boot state. Reading the RAW key eFuse from NS
     * stalls (TZ + eFuse power domain), so read the AUTOLOAD registers the bootloader uses instead:
     * REG_LP_EFUSE_PROTECTION (efuse 0x1C0/1 security config; all-1s = blank) + REG_SYS_EFUSE_SYSCFG3
     * (OTF/RSIP enable). NS-readable. */
    if (sublen == 6 && strncmp(sub, "secchk", 6) == 0) {
        u32 prot    = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_LP_EFUSE_PROTECTION);
        u32 syscfg3 = HAL_READ32(SYSTEM_CTRL_BASE_LP, REG_SYS_EFUSE_SYSCFG3);
        log_info("[EFUSE] protection reg (efuse 0x1C0/1) = 0x%08x", (unsigned) prot);
        log_info("[EFUSE] syscfg3 reg                    = 0x%08x", (unsigned) syscfg3);
        log_info("[EFUSE] Secure Boot (IMG2)             = %s",
                 (prot & BIT_SECURE_IMG2_DIS) ? "OFF (disabled bit set)" : "ON (ENABLED)");
        log_info("[EFUSE] RSIP/OTF flash encryption      = %s",
                 (syscfg3 & BIT_SYS_FLASH_ENCRYPT_EN) ? "ON (ENABLED)" : "OFF (plain image)");
        snprintf(out, outlen, "efuse enable-state -> see [EFUSE] log\r\n");
        return 0;
    }

    return -1;   /* not an AmebaD verb */
}

/* -------------------------------------------- TF-M PSA diagnostics (opt) ---- */
#ifdef AMEBAD_TFM_STORAGE
/* `pstest [size] [retries] [use_its] [uidsel]`: write <size> bytes to PS (or ITS) at runtime, retried,
 * report status + elapsed ms. Probes the runtime PS/ITS write blocker (SPE flash vs KM0 XIP). */
static long parse_arg(const char *cmd, UBaseType_t idx, long dflt)
{
    BaseType_t  l = 0;
    const char *p = FreeRTOS_CLIGetParameter(cmd, idx, &l);
    char        tmp[16];
    if (!p || l <= 0) return dflt;
    if ((size_t) l >= sizeof(tmp)) l = sizeof(tmp) - 1;
    memcpy(tmp, p, (size_t) l);
    tmp[l] = '\0';
    return strtol(tmp, NULL, 0);
}

static BaseType_t cmd_pstest(char *out, size_t outlen, const char *cmd)
{
    static uint8_t buf[4096];
    long sz     = parse_arg(cmd, 1, 256);
    long tries  = parse_arg(cmd, 2, 1);
    long use_its = parse_arg(cmd, 3, 0);   /* arg3=1 -> ITS (the real aws key path); else PS */
    long uidsel  = parse_arg(cmd, 4, 0);   /* arg4: 0=pstest uid  1=AWS_KEY_UID  2=low test uid */
    psa_storage_uid_t uid;
    psa_status_t s = PSA_ERROR_GENERIC_ERROR;
    long got = 0;
    TickType_t t0, dt;

    if (sz < 1) sz = 1;
    if (sz > (long) sizeof(buf)) sz = (long) sizeof(buf);
    if (tries < 1) tries = 1;
    if (tries > 50) tries = 50;
    for (long i = 0; i < sz; i++) buf[i] = (uint8_t) (i & 0xff);

    uid = (psa_storage_uid_t) 0x70737401ULL;                       /* "pst\1" (works) */
    if (uidsel == 1)      uid = (psa_storage_uid_t) 0x4157535F4B455901ULL;  /* == AWS_KEY_UID */
    else if (uidsel == 2) uid = (psa_storage_uid_t) 0x70770003ULL;          /* low value */

    t0 = xTaskGetTickCount();
    for (long t = 1; t <= tries; t++) {
        s   = use_its ? psa_its_set(uid, (size_t) sz, buf, 0) : psa_ps_set(uid, (size_t) sz, buf, 0);
        got = t;
        if (s == PSA_SUCCESS) break;
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    dt = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
    if (use_its) (void) psa_its_remove(uid); else (void) psa_ps_remove(uid);

    snprintf(out, outlen, "pstest %ldB x%ld %s -> %s on try %ld (status=%d, %u ms)\r\n",
             sz, tries, use_its ? "ITS" : "PS", (s == PSA_SUCCESS) ? "OK" : "FAIL",
             got, (int) s, (unsigned) dt);
    return pdFALSE;
}

/* `xiptest`: FNV-hash the NS XIP code region before/after a runtime psa_its_set (RAM-resident probe,
 * AN0400 §18.2). CHANGED => the runtime secure flash write corrupts the NS XIP read path. */
IMAGE2_RAM_TEXT_SECTION
static uint32_t xip_csum(uint32_t base, uint32_t len, uint8_t *win, uint32_t winoff, uint32_t winlen)
{
    const volatile uint8_t *p = (const volatile uint8_t *) base;
    uint32_t s = 0x811c9dc5u;   /* FNV-1a */
    uint32_t i;
    for (i = 0; i < len; i++) s = (s ^ p[i]) * 16777619u;
    p = (const volatile uint8_t *) (base + winoff);
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
    const psa_storage_uid_t uid = (psa_storage_uid_t) 0x5849500001ULL;  /* "XIP\0\1" */
    volatile uint32_t *r = (volatile uint32_t *) 0x10002020u;   /* RSVD_RAM_NS verdict slots */
    uint32_t c0, c1;
    psa_status_t s;
    int i, diff = -1;
    (void) cmd; (void) outlen;

    r[0] = 0xC0DE0000u; r[7] = 0u; __DSB();
    for (i = 0; i < 256; i++) buf[i] = (uint8_t) i;
    c0 = xip_csum(base, len, w0, winoff, 256);
    r[1] = c0; __DSB();
    log_info("[XIP] c0=%08x, writing...", (unsigned) c0);
    s = psa_its_set(uid, 256, buf, PSA_STORAGE_FLAG_NONE);
    r[2] = (uint32_t) s; __DSB();
    log_info("[XIP] set=%d, re-reading...", (int) s);
    c1 = xip_csum(base, len, w1, winoff, 256);
    for (i = 0; i < 256; i++) if (w0[i] != w1[i]) { diff = i; break; }
    r[3] = c1;
    r[4] = (c0 == c1) ? 0x5A4D5A4Du : 0xBAD0BAD0u;       /* SAME / CHANGED */
    r[5] = (uint32_t) diff;
    r[6] = (diff >= 0) ? (((uint32_t) w0[diff] << 8) | w1[diff]) : 0xFFFFFFFFu;
    r[7] = 0xC0DEFFFFu; __DSB();
    log_info("[XIP] c1=%08x %s", (unsigned) c1, (c0 == c1) ? "SAME" : "CHANGED");
    if (diff >= 0)
        log_info("[XIP] win 0x%08x +%d: %02x -> %02x", (unsigned) (base + winoff), diff, w0[diff], w1[diff]);
    (void) psa_its_remove(uid);
    log_info("[XIP] done (survived)");
    out[0] = '\0';
    return pdFALSE;
}

void aws_platform_diag_register(void)
{
    static const CLI_Command_Definition_t cmd_xiptest_def = {
        "xiptest", "xiptest:\r\n RAM-resident: checksum NS XIP before/after a runtime ITS write\r\n",
        cmd_xiptest, 0
    };
    static const CLI_Command_Definition_t cmd_pstest_def = {
        "pstest", "pstest [size] [retries]:\r\n diagnostic: runtime PS write of <size> bytes, retried\r\n",
        cmd_pstest, -1
    };
    FreeRTOS_CLIRegisterCommand(&cmd_xiptest_def);
    FreeRTOS_CLIRegisterCommand(&cmd_pstest_def);
}
#else
void aws_platform_diag_register(void) { }
#endif /* AMEBAD_TFM_STORAGE */
