/*
 * aws_cli.c - shared `aws` console command (both AmebaD and Bouffalo), over FreeRTOS-Plus-CLI.
 *   aws                       -> provisioning status
 *   aws endpoint <host>       -> env "endpoint"
 *   aws ca   <base64-DER>     -> server CA  (aws_store_ca)
 *   aws cert <base64-DER>     -> client cert (aws_store_cert)
 *   aws key  <base64-DER>     -> client key  (aws_store_key -> secret-store backend; device-idle only)
 * Any other sub-verb is dispatched to the weak aws_cli_platform() hook: bl602 adds `start`; AmebaD
 * adds `blemac`/`secchk` (+ TF-M `pstest`/`xiptest`). The cert/key/ca base64 body is ~1.6 KB and
 * arrives as ONE CLI argument, so the console needs a large line buffer (MONITOR_CLI_MAX_INPUT) +
 * host-side pacing (see aws_provision.ps1).
 */
#include <stdio.h>
#include <stdlib.h>       /* setenv / getenv */
#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"

#include "aws_iot.h"

/* Weak platform hook for extra sub-verbs. Fill `out` and return 0 if handled; -1 if unrecognized. */
__attribute__((weak)) int aws_cli_platform(const char *sub, size_t sublen,
                                           const char *arg, size_t arglen,
                                           char *out, size_t outlen)
{
    (void) sub; (void) sublen; (void) arg; (void) arglen; (void) out; (void) outlen;
    return -1;
}

static BaseType_t cmd_aws(char *out, size_t outlen, const char *cmd)
{
    BaseType_t   sublen = 0, arglen = 0;
    const char * sub = FreeRTOS_CLIGetParameter(cmd, 1, &sublen);
    const char * arg = FreeRTOS_CLIGetParameter(cmd, 2, &arglen);

    if (sub == NULL) {
        const char *ep = getenv("endpoint");
        const char *ce = getenv("cert");
        const char *ke = getenv("key");
        const char *ca = getenv("ca");
        snprintf(out, outlen,
                 "aws: endpoint=%s cert=%s key=%s ca=%s\r\n"
                 "  aws endpoint <ep> | aws ca/cert/key <b64>\r\n",
                 (ep && ep[0]) ? ep : "(unset)", (ce && ce[0]) ? "set" : "(unset)",
                 (ke && ke[0]) ? "set" : "(unset)", (ca && ca[0]) ? "set" : "(unset)");
        return pdFALSE;
    }

    if (sublen == 8 && strncmp(sub, "endpoint", 8) == 0) {
        char ep[128];
        if (arg == NULL) { snprintf(out, outlen, "usage: aws endpoint <host>\r\n"); return pdFALSE; }
        if ((size_t) arglen >= sizeof(ep)) arglen = sizeof(ep) - 1;
        memcpy(ep, arg, arglen); ep[arglen] = '\0';
        snprintf(out, outlen, "aws endpoint %s -> %s\r\n", ep, (setenv("endpoint", ep, 1) == 0) ? "OK" : "FAIL");
    } else if (sublen == 4 && strncmp(sub, "cert", 4) == 0) {
        int rc = (arg != NULL) ? aws_store_cert(arg, (size_t) arglen) : -1;
        snprintf(out, outlen, "aws cert -> %s (%d)\r\n", rc == 0 ? "OK" : "FAIL", rc);
    } else if (sublen == 3 && strncmp(sub, "key", 3) == 0) {
        int rc = (arg != NULL) ? aws_store_key(arg, (size_t) arglen) : -1;
        snprintf(out, outlen, "aws key -> %s (%d)\r\n", rc == 0 ? "OK" : "FAIL", rc);
    } else if (sublen == 2 && strncmp(sub, "ca", 2) == 0) {
        int rc = (arg != NULL) ? aws_store_ca(arg, (size_t) arglen) : -1;
        snprintf(out, outlen, "aws ca -> %s (%d)\r\n", rc == 0 ? "OK" : "FAIL", rc);
    } else if (aws_cli_platform(sub, (size_t) sublen, arg, (size_t) arglen, out, outlen) == 0) {
        /* handled by the platform hook (bl602 `start`; AmebaD `blemac`/`secchk`/...) */
    } else {
        snprintf(out, outlen, "usage: aws [endpoint|cert|key|ca|...] ...\r\n");
    }
    return pdFALSE;
}

void aws_cli_register(void)
{
    static const CLI_Command_Definition_t cmd_aws_def = {
        "aws",
        "aws:\r\n endpoint/cert/key/ca <..> = provision; (none) = status\r\n",
        cmd_aws, -1
    };
    FreeRTOS_CLIRegisterCommand(&cmd_aws_def);
}
