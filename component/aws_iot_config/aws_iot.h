/*
 * aws_iot.h - the minimal AWS IoT telemetry client surface (component/aws_iot_config).
 * Credential model mirrors AmebaD's non-TF-M path: endpoint -> env "endpoint"; client cert + key ->
 * LittleFS DER files (paths published as env "cert"/"key"); server trust = embedded Amazon Root CA 1.
 */
#ifndef AWS_IOT_H
#define AWS_IOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AWS_CERT_PATH   "aws_cert.der"    /* client certificate DER (LittleFS) */
#define AWS_KEY_PATH    "aws_key.der"     /* client private key DER (LittleFS file backend) */
#define AWS_CA_PATH     "aws_ca.der"      /* server-trust Amazon Root CA 1 DER (LittleFS) */
#ifndef AWS_KEY_MAX
#define AWS_KEY_MAX     2560              /* max DER blob (cert/CA/key); AmebaD -D overrides to 1536 (heap) */
#endif
#define AWS_CERT_MAX    AWS_KEY_MAX
#define AWS_ENDPOINT_MAX 128             /* max ATS endpoint hostname */

/* Provisioning (aws_provision.c): the console passes the credential's bare base64 body, decoded to
 * DER; cert/CA -> LittleFS file (path in env "cert"/"ca"); key -> the secret-store backend
 * (aws_secret_store.h: file or TF-M ITS). 0 = ok. */
int aws_store_cert(const char *b64, size_t len);          /* -> AWS_CERT_PATH, setenv "cert" */
int aws_store_key(const char *b64, size_t len);           /* -> secret-store backend, setenv "key" */
int aws_store_ca(const char *b64, size_t len);            /* -> AWS_CA_PATH,   setenv "ca"   */
int aws_load_key(char *buf, size_t buf_len, size_t *out_len);   /* DER key into buf, *out_len = bytes */

/* Optional pre-scheduler init (weak no-op; AmebaD TF-M overrides to create a PSA mutex). */
void aws_provision_init(void);

/* Weak CLI platform hook (aws_cli.c): handle an `aws <sub>` verb not in the shared set (bl602 `start`;
 * AmebaD `blemac`/`secchk`/...). Fill `out`, return 0 if handled, -1 if unrecognized. */
int aws_cli_platform(const char *sub, size_t sublen, const char *arg, size_t arglen,
                     char *out, size_t outlen);

/* Platform MAC hook (Thing name / MQTT client ID / topic). Fills "aabbccddeeff"+NUL. 0 = ok.
 * Implemented per platform (bl602: wifi_mgmr_sta_mac_get; AmebaD: aws_get_ble_mac efuse). */
int aws_get_mac(char *out, size_t out_len);

/* Start the telemetry task (aws_telemetry.c, bl602). Idempotent + no-op until provisioned, so the
 * network-up hook can call it unconditionally on every DHCP-up. Self-heals until SNTP time. */
void aws_telemetry_start(void);

/* True once endpoint + client key are provisioned (the auto-start gate). */
int aws_is_provisioned(void);

/* Register the shared `aws` console command (aws_cli.c). */
void aws_cli_register(void);

#ifdef __cplusplus
}
#endif

#endif /* AWS_IOT_H */
