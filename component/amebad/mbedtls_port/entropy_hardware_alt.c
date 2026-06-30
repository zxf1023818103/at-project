/*
 * entropy_hardware_alt.c — AmebaD MBEDTLS_ENTROPY_HARDWARE_ALT source.
 *
 * Supplies mbedtls_hardware_poll() so CTR_DRBG/keygen have an entropy source on a target
 * with no /dev/urandom (MBEDTLS_NO_PLATFORM_ENTROPY). Platform-specific, so it stays in the
 * AmebaD port (the FreeRTOS threading glue is generic and lives in component/mbedtls_config).
 *
 * NOTE: this mirrors the wolfcrypt_glue.c stance — a fast xorshift, NOT crypto-grade. It
 * exists so randomness has *a* source; a real TRNG (RTL8721D TRNG / eFuse seed) is the
 * follow-up. Do not ship secrets generated from this as-is.
 */
#include <stdint.h>
#include <stddef.h>

int mbedtls_hardware_poll( void *data, unsigned char *output, size_t len, size_t *olen )
{
    static uint32_t s = 0x9E3779B9u;   /* seeded; per-boot variation is a TRNG follow-up */
    size_t i;

    ( void ) data;
    for( i = 0; i < len; i++ )
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        output[ i ] = ( unsigned char ) ( s & 0xFFu );
    }
    if( olen != NULL )
    {
        *olen = len;
    }
    return 0;
}
