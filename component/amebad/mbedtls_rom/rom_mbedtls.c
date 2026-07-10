/*
 * AmebaD ROM mbedTLS 2.4.0 ram-map glue — see rom_mbedtls.h.
 *
 * rom_ssl_ram_map / p_rom_ssl_ram_map are mask-ROM BSS symbols
 * (rlx8721d_rom_symbol_acut.ld: rom_ssl_ram_map=0x1000134c, p_=0x10001348),
 * declared extern by rom_ssl_ram_map.h.
 *
 * Allocation goes through the FreeRTOS heap (pvPortMalloc), which is the image's
 * real, always-working heap — the SDK likewise routes mbedtls calloc -> rtw_calloc
 * -> pvPortMalloc (newlib calloc/_sbrk is not retargeted here). The alloc-using
 * primitives (RSA / GCM / PK / cipher_setup) therefore need the scheduler running;
 * AES / SHA / MD do not allocate.
 */
#include "rom_mbedtls.h"
#include <string.h>
#include "FreeRTOS.h"
#include "rom_ssl_ram_map.h"

/* calloc over the FreeRTOS heap (zero-initialised, as callers expect). */
static void *rom_ssl_calloc(unsigned int n, unsigned int size)
{
    unsigned int total = n * size;
    void *p = pvPortMalloc(total);
    if (p != NULL)
        memset(p, 0, total);
    return p;
}

/*
 * mbedTLS 2.4.0 PLATFORM_MEMORY form: the function-pointer symbols the library
 * refers to for allocation on some paths. Provide them over the FreeRTOS heap.
 */

void * (*mbedtls_calloc)( size_t, size_t ) = (void *(*)(size_t, size_t))rom_ssl_calloc;
void   (*mbedtls_free)( void * )           = vPortFree;

void rom_mbedtls_init(void)
{
    static int done = 0;
    if (done)
        return;
    done = 1;

    /*
     * Both struct views overlay the SAME ROM storage (p_rom_ssl_ram_map ==
     * &rom_ssl_ram_map). The mbedTLS-ROM code allocates via the _rom_mbedtls_ram_map
     * view: offset 0 = ssl_calloc, offset 4 = ssl_free.
     */
    p_rom_ssl_ram_map = (struct _rom_mbedtls_ram_map *)&rom_ssl_ram_map;
    p_rom_ssl_ram_map->ssl_calloc = rom_ssl_calloc;
    p_rom_ssl_ram_map->ssl_free   = vPortFree;

    /*
     * The HW/SW gate is read by the ROM aes/gcm/des routines through the
     * _rom_ssl_ram_map view (different field offset than the mbedtls view), so it
     * must be set on THIS view. 0 => software path (ROM tables); the IPSEC engine
     * is never poked.
     */
    rom_ssl_ram_map.ssl_malloc = (void *(*)(unsigned int))pvPortMalloc;
    rom_ssl_ram_map.ssl_free   = vPortFree;
    rom_ssl_ram_map.use_hw_crypto_func = 0;
}

/*
 * Provide mbedtls_platform_set_calloc_free OURSELVES. lib_wlan.a's WPA3/SAE crypto object
 * (rtw_opt_crypto_ssl.o, built against Mbed TLS 2.4.0) references this symbol via
 * rtw_platform_set_calloc_free. It is the ONE mbedTLS symbol the SAE path needs that is absent from
 * the ROM 2.4.0 export (mbedtls_rom.ld) -- so if we DON'T define it, the linker pulls in the upstream
 * software mbedcrypto (3.6.x) to satisfy it, and that then STRONG-overrides every ROM PROVIDE. The
 * SAE's mbedtls ecp/mpi calls would bind to 3.6.x (mbedtls_mpi = 8 B, p first) while the 2.4.0 SAE
 * code lays them out as 12 B (s,n,p) -> corrupted limb pointers -> heap fault. Defining it here
 * keeps everything on the ROM 2.4.0, exactly like the Realtek factory image. It routes the driver's
 * allocator through the ROM ram-map view the ROM mbedTLS actually allocates from.
 */
int mbedtls_platform_set_calloc_free(void *(*calloc_func)(size_t, size_t), void (*free_func)(void *))
{
    rom_mbedtls_init();
    if (calloc_func) p_rom_ssl_ram_map->ssl_calloc = (void *(*)(unsigned int, unsigned int))calloc_func;
    if (free_func)   p_rom_ssl_ram_map->ssl_free   = free_func;
    return 0;
}
