/*
 * crt_bundle_cli.c — `crt_bundle` console command (make AWS=ON CRT_BUNDLE=ON).
 *
 * Demonstrates / validates the compressed Mozilla CA root bundle (component/esp_crt_bundle_config)
 * on-device, independent of a live TLS connection:
 *
 *   crt_bundle info               # cert count + compressed blob size (proves the compression)
 *   crt_bundle verify <pem> [cn]  # parse a saved server cert chain (PEM/DER) from LittleFS and
 *                                 # verify it against the bundle; optional [cn] = expected hostname
 *   crt_bundle addca <name> <b64> # write a trusted-root CA (base64 of a PEM/DER cert) into the
 *                                 # `ca/` folder at runtime (no reflash); picked up by the next TLS
 *                                 # connection. Remove with `rm ca/<name>`.
 *   crt_bundle connect <host> <p> # TLS-handshake to host:port (trust = bundle + ca/ folder), print
 *                                 # the server cert Subject/Issuer + PASS/FAIL. No HTTP, no flash.
 *
 * The `verify` path uses mbedtls_x509_crt_parse_file (MBEDTLS_FS_IO is on) + the same
 * esp_crt_bundle_verify_chain() the picotls backend uses, so a PASS here is the exact check the
 * TLS handshake performs — minus the network. Save a chain on the device first, e.g.
 *   openssl s_client -showcerts -connect example.com:443 </dev/null  (paste the certs)  -> ls /chain.pem
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>          /* atoi */

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"

#include "mbedtls/x509_crt.h"
#include "mbedtls/base64.h"  /* addca: base64 -> DER */
#include "esp_crt_bundle.h"
#include "transport_tls.h"   /* connect: TLS_FreeRTOS_Connect/Disconnect + NetworkCredentials_t */

/* The TLS transport leaves struct NetworkContext incomplete; each consumer defines it (same as
 * ota_http.c / aws_iot_demo.c). It just carries the TlsTransportParams_t the transport uses. */
struct NetworkContext
{
    TlsTransportParams_t * pParams;
};

static BaseType_t cmd_crt_bundle(char *out, size_t outlen, const char *cmd)
{
    BaseType_t subLen = 0;
    const char *sub = FreeRTOS_CLIGetParameter(cmd, 1, &subLen);

    if (sub == NULL) {
        snprintf(out, outlen, "usage: crt_bundle info | crt_bundle verify <pem-path> [hostname]\r\n");
        return pdFALSE;
    }

    if ((subLen == 4) && (strncmp(sub, "info", 4) == 0)) {
        uint32_t n = esp_crt_bundle_count();
        uint32_t sz = esp_crt_bundle_blob_size();
        log_info("[CRTB] embedded Mozilla CA bundle: %u roots, %u bytes (compressed CN+pubkey)",
                 (unsigned)n, (unsigned)sz);
        snprintf(out, outlen, "crt_bundle: %u roots, %u bytes blob\r\n", (unsigned)n, (unsigned)sz);
        return pdFALSE;
    }

    if ((subLen == 6) && (strncmp(sub, "verify", 6) == 0)) {
        BaseType_t pathLen = 0;
        const char *path = FreeRTOS_CLIGetParameter(cmd, 2, &pathLen);
        if (path == NULL) {
            snprintf(out, outlen, "usage: crt_bundle verify <pem-path> [hostname]\r\n");
            return pdFALSE;
        }
        char pathBuf[128];
        if ((size_t)pathLen >= sizeof(pathBuf)) {
            snprintf(out, outlen, "crt_bundle: path too long\r\n");
            return pdFALSE;
        }
        memcpy(pathBuf, path, pathLen);
        pathBuf[pathLen] = '\0';

        /* optional hostname (CN check); NULL skips it */
        BaseType_t cnLen = 0;
        const char *cnArg = FreeRTOS_CLIGetParameter(cmd, 3, &cnLen);
        char cnBuf[80];
        const char *cn = NULL;
        if ((cnArg != NULL) && ((size_t)cnLen < sizeof(cnBuf))) {
            memcpy(cnBuf, cnArg, cnLen);
            cnBuf[cnLen] = '\0';
            cn = cnBuf;
        }

        mbedtls_x509_crt chain;
        mbedtls_x509_crt_init(&chain);
        int rc = mbedtls_x509_crt_parse_file(&chain, pathBuf);
        if (rc < 0) {
            log_error("[CRTB] parse '%s' failed: -0x%04x", pathBuf, (unsigned)-rc);
            snprintf(out, outlen, "crt_bundle: parse failed (-0x%04x)\r\n", (unsigned)-rc);
            mbedtls_x509_crt_free(&chain);
            return pdFALSE;
        }

        uint32_t flags = 0;
        int vr = esp_crt_bundle_verify_chain(&chain, NULL, cn, &flags);
        log_info("[CRTB] verify '%s' (cn=%s): rc=%d flags=0x%08x -> %s",
                 pathBuf, cn ? cn : "(none)", vr, (unsigned)flags,
                 (vr == 0) ? "PASS (trusted by bundle)" : "FAIL");
        snprintf(out, outlen, "crt_bundle: verify=%s flags=0x%08x -> see [CRTB] log\r\n",
                 (vr == 0) ? "PASS" : "FAIL", (unsigned)flags);
        mbedtls_x509_crt_free(&chain);
        return pdFALSE;
    }

    if ((subLen == 5) && (strncmp(sub, "addca", 5) == 0)) {
        BaseType_t nameLen = 0, b64Len = 0;
        const char *name = FreeRTOS_CLIGetParameter(cmd, 2, &nameLen);
        const char *b64  = FreeRTOS_CLIGetParameter(cmd, 3, &b64Len);
        if ((name == NULL) || (b64 == NULL)) {
            snprintf(out, outlen, "usage: crt_bundle addca <name> <base64-cert>\r\n");
            return pdFALSE;
        }
        char path[160];
        if ((size_t)nameLen >= sizeof(path) - 4) {
            snprintf(out, outlen, "crt_bundle: name too long\r\n");
            return pdFALSE;
        }
        memcpy(path, "ca/", 3);
        memcpy(path + 3, name, nameLen);
        path[3 + nameLen] = '\0';

        static unsigned char der[2560];
        size_t derLen = 0;
        int r = mbedtls_base64_decode(der, sizeof(der), &derLen,
                                      (const unsigned char *)b64, (size_t)b64Len);
        if (r != 0) {
            log_error("[CRTB] addca '%s': base64 decode failed (-0x%04x)", path, (unsigned)-r);
            snprintf(out, outlen, "crt_bundle: base64 decode failed (-0x%04x)\r\n", (unsigned)-r);
            return pdFALSE;
        }
        FILE *f = fopen(path, "w");
        if (f == NULL) {
            snprintf(out, outlen, "crt_bundle: cannot open %s\r\n", path);
            return pdFALSE;
        }
        size_t wrote = fwrite(der, 1, derLen, f);
        fclose(f);
        log_info("[CRTB] addca wrote %s (%u bytes DER)", path, (unsigned)wrote);
        snprintf(out, outlen, "crt_bundle: wrote %s (%u bytes)\r\n", path, (unsigned)wrote);
        return pdFALSE;
    }

    if ((subLen == 7) && (strncmp(sub, "connect", 7) == 0)) {
        BaseType_t hostLen = 0, portLen = 0;
        const char *host = FreeRTOS_CLIGetParameter(cmd, 2, &hostLen);
        const char *portS = FreeRTOS_CLIGetParameter(cmd, 3, &portLen);
        if ((host == NULL) || (portS == NULL)) {
            snprintf(out, outlen, "usage: crt_bundle connect <host> <port>\r\n");
            return pdFALSE;
        }
        char hostBuf[80], portBuf[8];
        if (((size_t)hostLen >= sizeof(hostBuf)) || ((size_t)portLen >= sizeof(portBuf))) {
            snprintf(out, outlen, "crt_bundle: host/port too long\r\n");
            return pdFALSE;
        }
        memcpy(hostBuf, host, hostLen); hostBuf[hostLen] = '\0';
        memcpy(portBuf, portS, portLen); portBuf[portLen] = '\0';

        /* TLS handshake only: trust = embedded bundle + the `ca/` folder (pRootCaPath). No client
         * cert (servers under test don't ask for one). picotls_verify_cert logs Subject/Issuer +
         * PASS/FAIL during the handshake. */
        NetworkCredentials_t creds;
        NetworkContext_t netCtx;
        TlsTransportParams_t params;
        memset(&creds, 0, sizeof(creds));
        memset(&netCtx, 0, sizeof(netCtx));
        memset(&params, 0, sizeof(params));
        creds.pRootCaPath = "ca";
        netCtx.pParams = &params;

        size_t h0 = xPortGetFreeHeapSize();
        TlsTransportStatus_t st = TLS_FreeRTOS_Connect(&netCtx, hostBuf, (uint16_t)atoi(portBuf),
                                                       &creds, 8000, 8000);
        if (st == TLS_TRANSPORT_SUCCESS) {
            size_t h1 = xPortGetFreeHeapSize();   /* heap held by the live TLS session */
            log_info("[CRTB] connect %s:%s -> PASS (cert trusted)", hostBuf, portBuf);
            log_info("[CRTB] heap: before=%u during=%u  TLS session used=%u bytes",
                     (unsigned)h0, (unsigned)h1, (unsigned)(h0 - h1));
            TLS_FreeRTOS_Disconnect(&netCtx);
            log_info("[CRTB] heap after disconnect=%u (recovered=%u)",
                     (unsigned)xPortGetFreeHeapSize(), (unsigned)(xPortGetFreeHeapSize() - h1));
            snprintf(out, outlen, "crt_bundle: connect %s:%s PASS (TLS used %u B)\r\n",
                     hostBuf, portBuf, (unsigned)(h0 - h1));
        } else {
            log_error("[CRTB] connect %s:%s -> FAIL (status=%d)", hostBuf, portBuf, (int)st);
            snprintf(out, outlen, "crt_bundle: connect %s:%s FAIL (status=%d)\r\n",
                     hostBuf, portBuf, (int)st);
        }
        return pdFALSE;
    }

    snprintf(out, outlen,
             "usage: crt_bundle info | verify <pem> [cn] | addca <name> <b64> | connect <host> <port>\r\n");
    return pdFALSE;
}

void crt_bundle_cli_register(void)
{
    static const CLI_Command_Definition_t cmd_crt_bundle_def = {
        "crt_bundle",
        "crt_bundle:\r\n info | verify <pem> [cn] | addca <name> <b64> | connect <host> <port>\r\n",
        cmd_crt_bundle, -1  /* variable parameter count */
    };
    FreeRTOS_CLIRegisterCommand(&cmd_crt_bundle_def);
}
