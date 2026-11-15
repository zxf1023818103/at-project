#include "wifi_api.h"
#include "wifi_api_ext.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "wifi_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int busy;
static char ssid[33], identity[64], pin[9];
static unsigned short wps_mode;
#ifdef AT_ENABLE_EAP
static volatile int eap_loop, eap_stop;
#endif
static int parameter(const char *cmd, unsigned index, char *dst, size_t size)
{
    BaseType_t len;
    const char *p = FreeRTOS_CLIGetParameter(cmd, index, &len);
    if (!p || len <= 0 || (size_t)len >= size) return -1;
    memcpy(dst, p, len); dst[len] = 0;
    return 0;
}
static int pause_service(void)
{
    if (wifi_service_pause(15000) == 0) return 0;
    printf("[PROVISION] Wi-Fi service did not pause\n");
    wifi_reconnect_enable();
    return -1;
}
#ifdef AT_ENABLE_WPS
#include "wifi_wps_config.h"
#include "wps_defs.h"
static void wps_task(void *arg)
{
    (void)arg;
    int rc = -1;
    if (!pause_service()) rc = wps_start(wps_mode, pin[0] ? pin : NULL, 0, NULL);
    if (!rc) {
        struct rtw_wifi_setting setting = {0};
        if (!wifi_get_setting(0, &setting)) {
            char value[16];
            setenv("WIFI_PASSWORD", (char *)setting.password, 1);
            setenv("WIFI_SSID", (char *)setting.ssid, 1);
            snprintf(value, sizeof(value), "%u", setting.channel);
            setenv("WIFI_CHANNEL", value, 1);
            snprintf(value, sizeof(value), "%lu", (unsigned long)setting.security_type);
            setenv("WIFI_SECURITY", value, 1);
        } else rc = -1;
    }
    printf("[WPS] result=%d%s\n", rc, rc ? "" : " credentials saved");
    wifi_reconnect_enable(); busy = 0; vTaskDelete(NULL);
}
static BaseType_t wps_command(char *out, size_t len, const char *cmd)
{
    char mode[8];
    if (busy) { snprintf(out, len, "provisioning is busy\r\n"); return pdFALSE; }
    if (parameter(cmd, 1, mode, sizeof(mode))) goto usage;
    pin[0] = 0;
    if (!strcmp(mode, "pbc")) wps_mode = WPS_CONFIG_PUSHBUTTON;
    else if (!strcmp(mode, "pin")) {
        if (parameter(cmd, 2, pin, sizeof(pin))) {
            snprintf(pin, sizeof(pin), "%08u", wps_generate_pin());
            wps_mode = WPS_CONFIG_DISPLAY;
        } else {
            if (strlen(pin) != 8 || strspn(pin, "0123456789") != 8) goto usage;
            unsigned n = (unsigned)strtoul(pin, NULL, 10), check = n % 10;
            n /= 10; unsigned sum = 0;
            for (unsigned i = 0; i < 7; ++i) { sum += (i % 2 ? 1 : 3) * (n % 10); n /= 10; }
            if ((10 - sum % 10) % 10 != check) goto usage;
            wps_mode = WPS_CONFIG_KEYPAD;
        }
    } else goto usage;
    busy = 1;
    if (xTaskCreate(wps_task, "wps", 4096, NULL, 2, NULL) != pdPASS) { busy = 0; goto usage; }
    snprintf(out, len, "WPS started%s%s\r\n", pin[0] ? ": PIN=" : "", pin);
    return pdFALSE;
usage:
    snprintf(out, len, "wps pbc | wps pin [8-digit checksum-valid PIN]\r\n"); return pdFALSE;
}
#endif
#ifdef AT_ENABLE_EAP
extern char *eap_target_ssid, *eap_identity, *eap_password, *eap_client_key_pwd;
extern const unsigned char *eap_client_cert, *eap_client_key, *eap_ca_cert;
extern int eap_client_cert_len, eap_client_key_len, eap_ca_cert_len;
extern int eap_start(char *method);
const char *dplus_eap_server_name;
static unsigned char *read_pem(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || n > 16384 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
    unsigned char *p = malloc(n+1);
    if (!p || fread(p, 1, n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f); p[n] = 0; *len = n+1; return p;
}
static void eap_task(void *arg)
{
    (void)arg;
    unsigned char *cert = read_pem("/eap/client.pem", &eap_client_cert_len);
    unsigned char *key = read_pem("/eap/key.pem", &eap_client_key_len);
    unsigned char *ca = read_pem("/eap/ca.pem", &eap_ca_cert_len);
    dplus_eap_server_name = getenv("EAP_SERVER_NAME");
    int rc = -1;
    if (!cert || !key || !ca || !dplus_eap_server_name || !*dplus_eap_server_name)
        printf("[EAP] requires /eap/client.pem, /eap/key.pem, /eap/ca.pem and EAP_SERVER_NAME\n");
    else if (!pause_service()) {
        eap_target_ssid = ssid; eap_identity = identity; eap_password = NULL;
        eap_client_cert = cert; eap_client_key = key; eap_ca_cert = ca; eap_client_key_pwd = NULL;
        unsigned attempt = 0;
        do {
            rc = eap_start("tls");
            printf("[EAP] TLS attempt=%u result=%d\n", ++attempt, rc);
            if (!eap_loop || eap_stop) break;
            for (unsigned i = 0; i < 50 && !eap_stop; ++i) vTaskDelay(pdMS_TO_TICKS(100));
            wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
        } while (!eap_stop);
    }
    /* The SDK parses PEM into owned mbedTLS objects before eap_start returns. */
    eap_client_cert = eap_client_key = eap_ca_cert = NULL;
    free(cert); free(key); free(ca);
    if (rc || eap_stop) wifi_reconnect_enable();
    busy = 0; vTaskDelete(NULL);
}
static BaseType_t eap_command(char *out, size_t len, const char *cmd)
{
    char action[33] = {0};
    if (!parameter(cmd, 1, action, sizeof(action)) && !strcmp(action, "stop")) {
        eap_stop = 1; eap_loop = 0;
        wifi_disconnect(); wifi_reconnect_enable();
        snprintf(out, len, "EAP stopped; stored Wi-Fi configuration resumed\r\n"); return pdFALSE;
    }
    if (busy) { snprintf(out, len, "provisioning is busy\r\n"); return pdFALSE; }
    unsigned index = !strcmp(action, "loop") ? 2 : 1;
    if (parameter(cmd, index, ssid, sizeof(ssid)) || parameter(cmd, index+1, identity, sizeof(identity))) {
        snprintf(out, len, "eap [loop] <ssid> <identity> | eap stop; PEM files in /eap/\r\n"); return pdFALSE;
    }
    eap_loop = index == 2; eap_stop = 0;
    busy = 1;
    if (xTaskCreate(eap_task, "eap", 4096, NULL, 2, NULL) != pdPASS) busy = 0;
    snprintf(out, len, "EAP %s\r\n", busy ? "started" : "task allocation failed"); return pdFALSE;
}
#endif
void wifi_provision_cli_register(void)
{
#ifdef AT_ENABLE_WPS
    static const CLI_Command_Definition_t wps = {"wps", "wps pbc | pin [PIN]\r\n", wps_command, -1};
    FreeRTOS_CLIRegisterCommand(&wps);
#endif
#ifdef AT_ENABLE_EAP
    static const CLI_Command_Definition_t eap = {"eap", "eap [loop] <ssid> <identity> | stop\r\n", eap_command, -1};
    FreeRTOS_CLIRegisterCommand(&eap);
#endif
}
