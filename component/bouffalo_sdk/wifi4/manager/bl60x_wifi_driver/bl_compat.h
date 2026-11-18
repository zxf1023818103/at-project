/*
 * bl_compat.h — minimal scalar shims for the bl60x wifi4 driver after lwIP was
 * removed (network stack switched to FreeRTOS-Plus-TCP). These are NOT lwIP: just
 * the return-code typedef / error enum / TX headroom constant / byte-order helpers
 * the driver's existing code uses, kept here so the firmware-facing driver logic
 * doesn't have to be rewritten line-by-line. The packet path (pbuf / netif) is
 * rewritten onto NetworkBufferDescriptor + the FreeRTOS-Plus-TCP NetworkInterface
 * (component/bouffalo_sdk/freertos_tcp_port/bl602_netif.c); this header only covers
 * the scalar bits.
 */
#ifndef BL_COMPAT_H
#define BL_COMPAT_H

#include <stdint.h>
/* The lwIP headers used to pull FreeRTOS.h in transitively (via lwip/sys.h). With lwIP
 * gone, the driver TUs that use FreeRTOS primitives (semphr/queue/task) need it first;
 * bl_compat.h is included ahead of those, so include it here. */
#include <FreeRTOS.h>

/* lwIP's err_t / error codes (values match lwIP so existing `== ERR_OK` etc. work). */
typedef int8_t err_t;
#define ERR_OK       0
#define ERR_MEM     -1
#define ERR_BUF     -2
#define ERR_TIMEOUT -3
#define ERR_RTE     -4
#define ERR_VAL     -6
#define ERR_USE     -8
#define ERR_IF      -12
#define ERR_CONN    -11

/* TX headroom the driver reserves for its bl_txhdr (was lwIP's PBUF_LINK_ENCAPSULATION_HLEN). */
#ifndef PBUF_LINK_ENCAPSULATION_HLEN
#define PBUF_LINK_ENCAPSULATION_HLEN 48
#endif

/* Byte-order helpers (were lwIP lwip_htons/htonl). Little-endian target. */
#ifndef lwip_htons
#define lwip_htons(x) __builtin_bswap16((uint16_t)(x))
#endif
#ifndef lwip_htonl
#define lwip_htonl(x) __builtin_bswap32((uint32_t)(x))
#endif
#ifndef lwip_ntohs
#define lwip_ntohs(x) __builtin_bswap16((uint16_t)(x))
#endif
#ifndef lwip_ntohl
#define lwip_ntohl(x) __builtin_bswap32((uint32_t)(x))
#endif

#endif /* BL_COMPAT_H */
