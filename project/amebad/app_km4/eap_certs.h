/*
 * eap_certs.h - EAP-TLS supplicant credentials for the `eap` console command
 *               (make EAP=ON). Reproduction bench only - NOT a shipping credential.
 *
 * These are the CLIENT cert + key the RTL8721D presents to the AP's RADIUS during
 * EAP-TLS. They MUST be signed by the CA the RADIUS server trusts. Server-cert
 * verification is OFF by default (platform_opts.h ENABLE_EAP_SSL_VERIFY_SERVER 0),
 * so EAP_CA_CERT_PEM is optional; define it (and set VERIFY_SERVER) to also check
 * the server.
 *
 * Generate the test PKI together with the AP (hostapd + FreeRADIUS / test RADIUS):
 *
 *   # CA
 *   openssl genrsa -out ca.key 2048
 *   openssl req -x509 -new -key ca.key -days 3650 -subj "/CN=at-eap-ca" -out ca.crt
 *   # client (identity = the CN; matches `eap <ssid> <identity>`)
 *   openssl genrsa -out client.key 2048
 *   openssl req -new -key client.key -subj "/CN=rtl8721d" -out client.csr
 *   openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
 *           -days 3650 -out client.crt
 *   # server (for hostapd's RADIUS)
 *   openssl genrsa -out server.key 2048
 *   openssl req -new -key server.key -subj "/CN=at-eap-server" -out server.csr
 *   openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
 *           -days 3650 -out server.crt
 *
 * Then paste client.crt / client.key below (PEM, one "\r\n"-terminated line each),
 * point hostapd's server_cert/private_key at server.crt/server.key and ca_cert at
 * ca.crt. A helper to emit this header from PEM files:
 *   awk '{printf "\"%s\\r\\n\" \\\n", $0}' client.crt
 */
#ifndef EAP_CERTS_H
#define EAP_CERTS_H

/* ====== PLACEHOLDER - replace with your generated client.crt before flashing ====== */
#define EAP_CLIENT_CERT_PEM \
"-----BEGIN CERTIFICATE-----\r\n" \
"REPLACE_WITH_client.crt\r\n" \
"-----END CERTIFICATE-----\r\n"

/* ====== PLACEHOLDER - replace with your generated client.key before flashing ======
 * Do NOT commit a real private key. Generate your own (recipe above) matching the
 * CA your RADIUS server trusts. */
#define EAP_CLIENT_KEY_PEM \
"-----BEGIN PRIVATE KEY-----\r\n" \
"REPLACE_WITH_client.key\r\n" \
"-----END PRIVATE KEY-----\r\n"

/* Optional: define EAP_CA_CERT_PEM (+ set ENABLE_EAP_SSL_VERIFY_SERVER 1) to verify
 * the RADIUS server cert too. Left undefined = client-auth-only EAP-TLS. */
/* #define EAP_CA_CERT_PEM "-----BEGIN CERTIFICATE-----\r\n" ... */

#endif /* EAP_CERTS_H */
