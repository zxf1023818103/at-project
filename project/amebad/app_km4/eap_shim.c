/*
 * eap_shim.c - link shims for the EAP-TLS supplicant (make EAP=ON) on this
 * FreeRTOS+TCP + upstream-Mbed-TLS build. lib_eap.a / wifi_eap_config.c /
 * tls_polarssl.c were written against the SDK's lwIP + the mbedTLS net-socket
 * layer; neither is present here, so provide the handful of symbols they pull.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* Upstream mbedTLS debug.c (pulled by tls_polarssl.c's mbedtls_ssl_conf_dbg) calls
 * the C-library vsnprintf, which this image never links: the AmebaD platform_stdlib
 * #defines vsnprintf -> _rtl_vsnprintf (SDK/ROM) everywhere else, so newlib's is
 * absent. Forward the real symbol to _rtl_vsnprintf. (debug.c does not include
 * platform_stdlib, so it references the unmapped name.) */
extern int _rtl_vsnprintf( char * str, size_t size, const char * fmt, va_list ap );
int vsnprintf( char * str, size_t size, const char * fmt, va_list ap )
{
    return _rtl_vsnprintf( str, size, fmt, ap );
}

/* lwIP byte-order helpers used by lib_eap.a's EAPOL framing (this build uses
 * FreeRTOS+TCP). Pure byte swaps - endianness only, no lwIP needed. */
uint16_t lwip_htons( uint16_t n ) { return __builtin_bswap16( n ); }
uint16_t lwip_ntohs( uint16_t n ) { return __builtin_bswap16( n ); }
uint32_t lwip_htonl( uint32_t n ) { return __builtin_bswap32( n ); }
uint32_t lwip_ntohl( uint32_t n ) { return __builtin_bswap32( n ); }

/* wifi_off() tears down the lwIP DHCP server; there is none in this image. */
void dhcps_deinit( void ) { }

/* tls_polarssl.c's tls_init/tls_deinit call the mbedTLS net-socket lifecycle, but
 * EAP-TLS runs over the in-memory buffer BIO (no real socket) - stub the ctx. */
void mbedtls_net_init( void * ctx ) { ( void ) ctx; }
void mbedtls_net_free( void * ctx ) { ( void ) ctx; }
