/*
 * eap_cli.c - on-device WPA2-Enterprise EAP-TLS connect test (make EAP=ON).
 *
 * Reproduces the customer (Kontakt.io) EAP-TLS association path on the RTL8721D:
 * the supplicant runs an EAP-TLS handshake (client cert) against the AP's RADIUS,
 * which is exactly where they see
 *     [eap_recvd] EAP TLS handshake step=13 ret=-0x7180   (INVALID_MAC)
 *     [wifi-ctrl] Wifi connect failed: -1056
 * eap_start() (wpa_supplicant/wifi_eap_config.c) blocks on the crypto-heavy
 * handshake, so run it on a dedicated task; the result lands in the [EAP] log.
 *
 *   eap <ssid> <identity>       - one EAP-TLS connect using the baked-in client
 *                                 cert/key (eap_certs.h)
 *   eap loop <ssid> <identity>  - reconnect in a loop (soak, to surface the
 *                                 intermittent INVALID_MAC) until `eap stop`
 *   eap stop                    - stop the loop
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "at_log.h"
#include "wifi/wifi_conf.h"        /* Realtek types + WiFi API - before supplicant headers */

#include "eap_certs.h"             /* EAP_CLIENT_CERT_PEM / EAP_CLIENT_KEY_PEM [ / EAP_CA_CERT_PEM ] */

/* wifi_eap_config.c globals (non-static) + entry point. */
extern char                *eap_target_ssid;
extern char                *eap_identity;
extern char                *eap_password;
extern const unsigned char *eap_client_cert;
extern const unsigned char *eap_client_key;
extern int                  eap_client_cert_len;
extern int                  eap_client_key_len;
extern char                *eap_client_key_pwd;
extern const unsigned char *eap_ca_cert;
extern int                  eap_ca_cert_len;
extern int                  eap_start( char * method );

/* MBEDTLS_THREADING_ALT is on: without registering the FreeRTOS mutex callbacks, every
 * mbedTLS mutex lock returns MBEDTLS_ERR_THREADING_BAD_INPUT_DATA (-0x001C) from the
 * fail-stub, which killed the EAP-TLS handshake at SERVER_KEY_EXCHANGE. AWS/QUIC images
 * arm this from their connect path; a plain EAP image must do it itself (once). */
extern void mbedtls_threading_freertos_init( void );

static char           s_ssid[ 33 ];
static char           s_identity[ 64 ];
static volatile int   s_loop;

static void eap_apply_certs( void )
{
    /* mbedTLS PEM parse wants the length to INCLUDE the terminating NUL, so sizeof()
     * (not strlen) of the string literal is correct here. */
    eap_client_cert     = ( const unsigned char * ) EAP_CLIENT_CERT_PEM;
    eap_client_cert_len = sizeof( EAP_CLIENT_CERT_PEM );
    eap_client_key      = ( const unsigned char * ) EAP_CLIENT_KEY_PEM;
    eap_client_key_len  = sizeof( EAP_CLIENT_KEY_PEM );
    eap_client_key_pwd  = NULL;                 /* unencrypted test key */
#ifdef EAP_CA_CERT_PEM
    eap_ca_cert     = ( const unsigned char * ) EAP_CA_CERT_PEM;
    eap_ca_cert_len = sizeof( EAP_CA_CERT_PEM );
#endif
}

static void eap_thread( void * param )
{
    ( void ) param;
    int n = 0;
    static int s_mtx_armed;

    if( !s_mtx_armed )
    {
        mbedtls_threading_freertos_init();   /* register FreeRTOS mutex callbacks (see note above) */
        s_mtx_armed = 1;
    }

    do
    {
        eap_apply_certs();
        eap_target_ssid = s_ssid;
        eap_identity    = s_identity;
        eap_password    = NULL;                 /* EAP-TLS authenticates with the cert, not a password */

        int rc = eap_start( "tls" );
        log_info( "[EAP] eap_start(tls) #%d rc=%d -> %s", ++n, rc,
                  ( rc == 0 ) ? "ASSOCIATED" : "failed (assoc -1056 / handshake -0x7180)" );

        if( s_loop )
            vTaskDelay( 3000 / portTICK_RATE_MS );
    } while( s_loop );

    vTaskDelete( NULL );
}

static BaseType_t cmd_eap_run( char * out, size_t outlen, const char * cmd )
{
    BaseType_t   l1 = 0;
    const char * a1 = FreeRTOS_CLIGetParameter( cmd, 1, &l1 );

    if( a1 == NULL )
    {
        snprintf( out, outlen, "usage: eap <ssid> <identity> | eap loop <ssid> <identity> | eap stop\r\n" );
        return pdFALSE;
    }
    if( strncmp( a1, "stop", 4 ) == 0 )
    {
        s_loop = 0;
        snprintf( out, outlen, "eap loop stop requested\r\n" );
        return pdFALSE;
    }

    int          loop = 0;
    const char * ssid;
    const char * ident;
    BaseType_t   ls = 0, li = 0;

    if( strncmp( a1, "loop", 4 ) == 0 )
    {
        loop  = 1;
        ssid  = FreeRTOS_CLIGetParameter( cmd, 2, &ls );
        ident = FreeRTOS_CLIGetParameter( cmd, 3, &li );
    }
    else
    {
        ssid  = a1;
        ls    = l1;
        ident = FreeRTOS_CLIGetParameter( cmd, 2, &li );
    }

    if( ssid == NULL || ident == NULL )
    {
        snprintf( out, outlen, "usage: eap <ssid> <identity> | eap loop <ssid> <identity>\r\n" );
        return pdFALSE;
    }

    /* FreeRTOS_CLIGetParameter returns a non-NUL-terminated slice; copy + terminate.
     * (The AmebaD SDK's _rtl_vsnprintf does NOT support the "%.*s" precision-from-arg
     * form - it prints the literal ".*s", which then became the scanned SSID.) */
    size_t ns = ( (size_t) ls < sizeof( s_ssid ) - 1 )     ? (size_t) ls : sizeof( s_ssid ) - 1;
    size_t ni = ( (size_t) li < sizeof( s_identity ) - 1 ) ? (size_t) li : sizeof( s_identity ) - 1;
    memcpy( s_ssid, ssid, ns );      s_ssid[ ns ] = '\0';
    memcpy( s_identity, ident, ni ); s_identity[ ni ] = '\0';
    s_loop = loop;

    if( xTaskCreate( eap_thread, "eap", 4096, NULL, tskIDLE_PRIORITY + 3, NULL ) != pdPASS )
        snprintf( out, outlen, "eap: xTaskCreate failed\r\n" );
    else
        snprintf( out, outlen, "eap-tls %s ssid=%s id=%s started -> see [EAP] log\r\n",
                  loop ? "loop" : "once", s_ssid, s_identity );

    return pdFALSE;
}

void eap_cli_register( void )
{
    static const CLI_Command_Definition_t cmd_eap_def = {
        "eap",
        "eap:\r\n eap <ssid> <id> | eap loop <ssid> <id> | eap stop - WPA2-Enterprise EAP-TLS connect\r\n",
        cmd_eap_run, -1
    };
    FreeRTOS_CLIRegisterCommand( &cmd_eap_def );
}
