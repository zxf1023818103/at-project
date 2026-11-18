#pragma once
/* WPA only needs IPv4 address representation and conversion, not BSD sockets. */
#include <stdint.h>
#include "FreeRTOS_IP.h"
#define AF_INET FREERTOS_AF_INET
struct in_addr { uint32_t s_addr; };
static inline char *inet_ntoa(struct in_addr address)
{
    static char buffer[16];
    FreeRTOS_inet_ntoa(address.s_addr, buffer);
    return buffer;
}
static inline int inet_aton(const char *text, struct in_addr *address)
{ return FreeRTOS_inet_pton(FREERTOS_AF_INET, text, &address->s_addr) == 1; }
#ifndef htons
#define htons FreeRTOS_htons
#define ntohs FreeRTOS_ntohs
#define htonl FreeRTOS_htonl
#define ntohl FreeRTOS_ntohl
#endif
/* WPA management structures contain IPv6 fields even in IPv4-only builds. */
#define AF_INET6 FREERTOS_AF_INET6
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
struct in6_addr { uint8_t s6_addr[16]; };
static inline const char *inet_ntop(int family, const void *source, char *dest, size_t length)
{
    if (family != AF_INET || length < INET_ADDRSTRLEN) return NULL;
    return FreeRTOS_inet_ntoa(*(const uint32_t *)source, dest);
}
static inline int inet_pton(int family, const char *text, void *address)
{ return family == AF_INET ? FreeRTOS_inet_pton(FREERTOS_AF_INET, text, address) : 0; }
struct sockaddr_in {
    uint16_t sin_family, sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};
