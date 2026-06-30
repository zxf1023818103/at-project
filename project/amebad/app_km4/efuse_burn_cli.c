/*
 * efuse_burn_cli.c — DEBUG / test-device PROVISIONING ONLY console command to BURN secure eFuse.
 *
 *   efuse_burn <addr_hex> <data_hex> CONFIRM
 *
 * Programs the bytes into secure eFuse via the TF-M EFUSE_WRITE IOCTL (the secure world runs the
 * ROM EFUSEWrite8), then reads them back via EFUSE_RAW and reports MATCH/MISMATCH. eFuse is
 * ONE-WAY IRREVERSIBLE - a wrong address/value cannot be undone. The literal CONFIRM token is
 * required. *** NEVER ship a build with this - it lets NS drive arbitrary secure eFuse burns. ***
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"
#include "tfm_platform_api.h"

#define AMEBAD_IOCTL_EFUSE_RAW    ((tfm_platform_ioctl_req_t)0x8721E2)
#define AMEBAD_IOCTL_EFUSE_WRITE  ((tfm_platform_ioctl_req_t)0x8721E3)

static int hexval(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

static int parse_hex(const char *s, int slen, uint8_t *outb, int maxb)
{
    int n = 0, i;
    if (slen <= 0 || (slen % 2) != 0) { return -1; }
    for (i = 0; i + 1 < slen; i += 2) {
        int hi = hexval(s[i]), lo = hexval(s[i + 1]);
        if (hi < 0 || lo < 0 || n >= maxb) { return -1; }
        outb[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

static BaseType_t cmd_efuse_burn(char *out, size_t outlen, const char *cmd)
{
    static const char hexd[] = "0123456789abcdef";
    BaseType_t la = 0, ld = 0, lc = 0;
    const char *a = FreeRTOS_CLIGetParameter(cmd, 1, &la);
    const char *d = FreeRTOS_CLIGetParameter(cmd, 2, &ld);
    const char *c = FreeRTOS_CLIGetParameter(cmd, 3, &lc);
    char abuf[12];
    uint8_t data[32], rb[32];
    char hex[2 * 32 + 1];
    uint32_t addr, ok = 0, req[2 + 32 / 4];
    int dlen, i;

    if (a == NULL || d == NULL || c == NULL || lc != 7 || strncmp(c, "CONFIRM", 7) != 0) {
        snprintf(out, outlen, "usage: efuse_burn <addr_hex> <data_hex> CONFIRM  (ONE-WAY irreversible!)\r\n");
        return pdFALSE;
    }
    if (la == 0 || (size_t)la >= sizeof(abuf)) {
        snprintf(out, outlen, "bad addr\r\n");
        return pdFALSE;
    }
    memcpy(abuf, a, (size_t)la);
    abuf[la] = '\0';
    addr = (uint32_t)strtoul(abuf, NULL, 16);
    dlen = parse_hex(d, (int)ld, data, (int)sizeof(data));
    if (dlen <= 0) {
        snprintf(out, outlen, "bad data hex (1..32 bytes, even length)\r\n");
        return pdFALSE;
    }

    req[0] = addr;
    req[1] = (uint32_t)dlen;
    memcpy(&req[2], data, (size_t)dlen);
    {
        psa_invec  in = { .base = req, .len = 2u * sizeof(uint32_t) + (size_t)dlen };
        psa_outvec ov = { .base = &ok, .len = sizeof(ok) };
        enum tfm_platform_err_t r = tfm_platform_ioctl(AMEBAD_IOCTL_EFUSE_WRITE, &in, &ov);
        log_info("[EFUSE] BURN 0x%x len=%d ioctl_ret=%d write_ok=%u", (unsigned)addr, dlen, (int)r, (unsigned)ok);
    }
    {
        uint32_t rreq[2] = { addr, (uint32_t)dlen };
        psa_invec  in = { .base = rreq, .len = sizeof(rreq) };
        psa_outvec ov = { .base = rb, .len = sizeof(rb) };
        (void)tfm_platform_ioctl(AMEBAD_IOCTL_EFUSE_RAW, &in, &ov);
        for (i = 0; i < dlen; i++) {
            hex[2 * i]     = hexd[rb[i] >> 4];
            hex[2 * i + 1] = hexd[rb[i] & 0x0Fu];
        }
        hex[2 * dlen] = '\0';
        log_info("[EFUSE] read-back 0x%x = %s  (%s)", (unsigned)addr, hex,
                 (memcmp(rb, data, (size_t)dlen) == 0) ? "MATCH" : "MISMATCH");
    }
    snprintf(out, outlen, "efuse_burn 0x%x done -> see [EFUSE] log\r\n", (unsigned)addr);
    return pdFALSE;
}

/* The LOGICAL eFuse map (autoload view) — different from the raw physical bytes efuse_burn writes.
 * Some controls (e.g. RSIP Enable = logical 0xE[2], AN0405) live only here and MUST be set via the
 * ROM logical-map write (which re-packs physical sections), not a raw physical burn. NS-side; no TF-M. */
extern int efuse_logical_read(uint16_t addr, uint16_t size, uint8_t *pbuf);
extern int efuse_logical_write(uint16_t addr, uint16_t size, uint8_t *data);

static const char EHEX[] = "0123456789abcdef";

static BaseType_t cmd_efuse_lread(char *out, size_t outlen, const char *cmd)
{
    BaseType_t la = 0, ll = 0;
    const char *a = FreeRTOS_CLIGetParameter(cmd, 1, &la);
    const char *l = FreeRTOS_CLIGetParameter(cmd, 2, &ll);
    char abuf[12], lbuf[8], hex[2 * 32 + 1];
    uint8_t buf[32];
    uint16_t addr, len;
    int rr, i;

    if (a == NULL || l == NULL || la == 0 || (size_t)la >= sizeof(abuf) || ll == 0 || (size_t)ll >= sizeof(lbuf)) {
        snprintf(out, outlen, "usage: efuse_lread <laddr_hex> <len(dec,1..32)>\r\n");
        return pdFALSE;
    }
    memcpy(abuf, a, (size_t)la); abuf[la] = '\0';
    memcpy(lbuf, l, (size_t)ll); lbuf[ll] = '\0';
    addr = (uint16_t)strtoul(abuf, NULL, 16);
    len  = (uint16_t)strtoul(lbuf, NULL, 10);
    if (len == 0u || len > 32u) { snprintf(out, outlen, "len 1..32\r\n"); return pdFALSE; }
    rr = efuse_logical_read(addr, len, buf);
    for (i = 0; i < (int)len; i++) { hex[2 * i] = EHEX[buf[i] >> 4]; hex[2 * i + 1] = EHEX[buf[i] & 0x0Fu]; }
    hex[2 * len] = '\0';
    log_info("[EFUSE] logical 0x%x [%u] = %s (ret=%d)", (unsigned)addr, (unsigned)len, hex, rr);
    snprintf(out, outlen, "efuse_lread 0x%x -> see [EFUSE] log\r\n", (unsigned)addr);
    return pdFALSE;
}

static BaseType_t cmd_efuse_lwrite(char *out, size_t outlen, const char *cmd)
{
    BaseType_t la = 0, ld = 0, lc = 0;
    const char *a = FreeRTOS_CLIGetParameter(cmd, 1, &la);
    const char *d = FreeRTOS_CLIGetParameter(cmd, 2, &ld);
    const char *c = FreeRTOS_CLIGetParameter(cmd, 3, &lc);
    char abuf[12], rbhex[2 * 32 + 1];
    uint8_t data[32], rb[32];
    uint16_t addr;
    int dlen, rc, i;

    if (a == NULL || d == NULL || c == NULL || lc != 7 || strncmp(c, "CONFIRM", 7) != 0) {
        snprintf(out, outlen, "usage: efuse_lwrite <laddr_hex> <data_hex> CONFIRM  (ONE-WAY)\r\n");
        return pdFALSE;
    }
    if (la == 0 || (size_t)la >= sizeof(abuf)) { snprintf(out, outlen, "bad addr\r\n"); return pdFALSE; }
    memcpy(abuf, a, (size_t)la); abuf[la] = '\0';
    addr = (uint16_t)strtoul(abuf, NULL, 16);
    dlen = parse_hex(d, (int)ld, data, (int)sizeof(data));
    if (dlen <= 0) { snprintf(out, outlen, "bad data hex (1..32 bytes)\r\n"); return pdFALSE; }
    rc = efuse_logical_write(addr, (uint16_t)dlen, data);
    (void)efuse_logical_read(addr, (uint16_t)dlen, rb);
    for (i = 0; i < dlen; i++) { rbhex[2 * i] = EHEX[rb[i] >> 4]; rbhex[2 * i + 1] = EHEX[rb[i] & 0x0Fu]; }
    rbhex[2 * dlen] = '\0';
    log_info("[EFUSE] LWRITE logical 0x%x rc=%d ; read-back = %s (%s)", (unsigned)addr, rc, rbhex,
             (memcmp(rb, data, (size_t)dlen) == 0) ? "MATCH" : "MISMATCH");
    snprintf(out, outlen, "efuse_lwrite 0x%x done -> see [EFUSE] log\r\n", (unsigned)addr);
    return pdFALSE;
}

void efuse_burn_cli_register(void)
{
    static const CLI_Command_Definition_t def = {
        "efuse_burn",
        "efuse_burn <addr_hex> <data_hex> CONFIRM:\r\n DEBUG: burn secure eFuse (ONE-WAY irreversible)\r\n",
        cmd_efuse_burn, -1
    };
    static const CLI_Command_Definition_t lrd = {
        "efuse_lread",
        "efuse_lread <laddr_hex> <len>:\r\n DEBUG: read the logical eFuse map\r\n",
        cmd_efuse_lread, -1
    };
    static const CLI_Command_Definition_t lwr = {
        "efuse_lwrite",
        "efuse_lwrite <laddr_hex> <data_hex> CONFIRM:\r\n DEBUG: write logical eFuse (ONE-WAY)\r\n",
        cmd_efuse_lwrite, -1
    };
    FreeRTOS_CLIRegisterCommand(&def);
    FreeRTOS_CLIRegisterCommand(&lrd);
    FreeRTOS_CLIRegisterCommand(&lwr);
}
