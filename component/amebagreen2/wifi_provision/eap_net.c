/* EAP uses an in-memory BIO; its SDK wrapper still creates an unused net ctx. */
#include "mbedtls/net_sockets.h"
#include <stdint.h>
uint16_t lwip_htons(uint16_t value) { return __builtin_bswap16(value); }
uint16_t lwip_ntohs(uint16_t value) { return __builtin_bswap16(value); }
uint32_t lwip_htonl(uint32_t value) { return __builtin_bswap32(value); }
uint32_t lwip_ntohl(uint32_t value) { return __builtin_bswap32(value); }
void mbedtls_net_init(mbedtls_net_context *ctx) { ctx->fd = -1; }
void mbedtls_net_free(mbedtls_net_context *ctx) { ctx->fd = -1; }
