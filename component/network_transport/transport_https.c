/*
 * transport_https.c — the single implementation of Https_Connect() (see transport_https.h).
 *
 * Deliberately backend-agnostic: it includes NO backend header and needs NO backend #define. It
 * calls the neutral (un-renamed) TLS_FreeRTOS_Connect, which the image resolves to whichever TLS
 * backend it linked (picotls / mbedTLS) or the fallback dispatcher — so this one .c compiles into
 * the base network_transport_sockets library and works for every build. NetworkContext_t is only
 * passed through as a pointer, so a forward declaration is enough.
 */
#include <string.h>

#include "transport_tls_common.h"   /* NetworkCredentials_t, TlsTransportStatus_t, TlsBackendMode_t */

struct NetworkContext;
typedef struct NetworkContext NetworkContext_t;

extern TlsTransportStatus_t TLS_FreeRTOS_Connect( NetworkContext_t * pNet, const char * host,
                                                  uint16_t port, const NetworkCredentials_t * creds,
                                                  uint32_t recvTmoMs, uint32_t sendTmoMs );

/* ---- runtime TLS backend (shared by every AWS/HTTPS connection) ---------------------------- */
static TlsBackendMode_t s_tls_backend = TLS_BACKEND_MODE_FALLBACK;   /* picotls-first by default */

void at_tls_set_backend( TlsBackendMode_t mode )
{
    if( ( mode == TLS_BACKEND_MODE_FALLBACK ) || ( mode == TLS_BACKEND_MODE_PICOTLS ) ||
        ( mode == TLS_BACKEND_MODE_MBEDTLS ) )
    {
        s_tls_backend = mode;
    }
}

TlsBackendMode_t at_tls_get_backend( void )
{
    return s_tls_backend;
}

TlsBackendMode_t at_tls_backend_from_str( const char * s )
{
    if( s != NULL )
    {
        if( strcmp( s, "picotls" ) == 0 ) { return TLS_BACKEND_MODE_PICOTLS; }
        if( strcmp( s, "mbedtls" ) == 0 ) { return TLS_BACKEND_MODE_MBEDTLS; }
    }
    return TLS_BACKEND_MODE_FALLBACK;
}

/* Amazon Root CA 1 (covers both AWS IoT and S3). Always compiled in: an AT_PROJECT_CRT_BUNDLE build
 * verifies via the embedded Mozilla bundle and IGNORES this pRootCa (transport_picotls.c), so
 * setting it is harmless there and it is the real trust anchor on a no-bundle build. ~1.2 KB. */
static const char HTTPS_AMAZON_ROOT_CA1[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\n"
    "-----END CERTIFICATE-----\n";

TlsTransportStatus_t Https_Connect( NetworkContext_t * pNet, const char * host, uint16_t port,
                                    size_t rxReserve, uint32_t recvTmoMs, uint32_t sendTmoMs )
{
    NetworkCredentials_t creds;

    memset( &creds, 0, sizeof( creds ) );
    creds.pRootCa       = ( const uint8_t * ) HTTPS_AMAZON_ROOT_CA1;
    creds.rootCaSize    = sizeof( HTTPS_AMAZON_ROOT_CA1 );   /* incl. NUL, as mbedTLS PEM wants */
    creds.rxReserveSize = rxReserve;
    creds.backendMode   = s_tls_backend;                     /* runtime-selected, shared with AWS MQTT */

    return TLS_FreeRTOS_Connect( pNet, host, port, &creds, recvTmoMs, sendTmoMs );
}
