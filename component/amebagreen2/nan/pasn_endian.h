#ifndef GREEN2_PASN_ENDIAN_H
#define GREEN2_PASN_ENDIAN_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
#define bswap_16 __builtin_bswap16
#define bswap_32 __builtin_bswap32
#define bswap_64 __builtin_bswap64
#define htonl __builtin_bswap32
#define ntohl __builtin_bswap32
#define htons __builtin_bswap16
#define ntohs __builtin_bswap16
#endif
