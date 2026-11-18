#pragma once
#include <stdint.h>
#ifndef htons
static inline uint16_t htons(uint16_t v) { return __builtin_bswap16(v); }
#endif
#ifndef ntohs
static inline uint16_t ntohs(uint16_t v) { return __builtin_bswap16(v); }
#endif
#ifndef htonl
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
#endif
#ifndef ntohl
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }
#endif
