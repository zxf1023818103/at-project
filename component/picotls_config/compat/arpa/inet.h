/*
 * Minimal compile-only <arpa/inet.h> shim for picotls on bare-metal newlib. Only the
 * presentation<->network address conversions picotls references; htons/ntohs live in the
 * netinet/in.h shim. inet_pton/inet_ntop are link-time consumer deps (unused on the QUIC
 * handshake path). See the note in the sibling sys/socket.h shim.
 */
#ifndef AT_PICOTLS_COMPAT_ARPA_INET_H
#define AT_PICOTLS_COMPAT_ARPA_INET_H

#include <netinet/in.h>

int         inet_pton( int af, const char *src, void *dst );
const char *inet_ntop( int af, const void *src, char *dst, socklen_t size );

#endif /* AT_PICOTLS_COMPAT_ARPA_INET_H */
