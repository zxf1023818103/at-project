/*
 * <arpa/inet.h> — address conversion + byte-order. htonl/htons/ntohl/ntohs and
 * inet_pton already live in component/syscall_config/newlib_syscalls.c (over
 * FreeRTOS_htonl etc.); inet_ntop is added in bsd_socket.c.
 */
#ifndef _AT_ARPA_INET_H
#define _AT_ARPA_INET_H

#include <netinet/in.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

int         inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
in_addr_t   inet_addr(const char *cp);
char       *inet_ntoa(struct in_addr in);

#ifdef __cplusplus
}
#endif

#endif /* _AT_ARPA_INET_H */
