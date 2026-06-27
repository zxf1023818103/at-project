/* cares_resolve.h — a synchronous, FreeRTOS+TCP-friendly front-end to c-ares.
 *
 * cares_resolve() runs one ares_getaddrinfo() driven by a select() event loop and copies the first
 * matching address into a struct freertos_sockaddr (port left 0; the caller sets it). It is mutex-
 * serialized because the BSD-shim select() uses a single static SocketSet_t (not reentrant).
 *
 * Built only when AT_PROJECT_CARES is defined (make CARES=ON). */
#ifndef AT_CARES_RESOLVE_H
#define AT_CARES_RESOLVE_H

struct freertos_sockaddr;

/* Create the c-ares channel (once). Safe to call repeatedly — only the first call initialises. */
void cares_resolver_init( void );

/* (Re)apply the DNS servers from the live IPv4 endpoint (DHCP-learned + the DNS_SERVER env
 * override). Call on every network-up so a roam picks up the new servers. */
void cares_resolver_set_servers( void );

/* Resolve host -> *out. family is AF_INET (IPv4 A), AF_INET6 (IPv6 AAAA) or AF_UNSPEC (either).
 * Returns 0 on success (out filled, sin_port = 0), <0 on failure. */
int cares_resolve( const char * host, int family, struct freertos_sockaddr * out );

#endif /* AT_CARES_RESOLVE_H */
