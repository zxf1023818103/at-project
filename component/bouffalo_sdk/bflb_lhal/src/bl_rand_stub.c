/* bl_rand_stub.c — weak fallback for the bl_rand() symbol the SDK's
 * BLE port + lwIP LWIP_RAND() macro reference.
 *
 * The upstream Bouffalo bl_port.c defines bl_rand() only when BL_MCU_SDK
 * is defined (using Sec_Eng_Trng_*). In our BOUFFALO_SDK-flavoured
 * build neither BL_MCU_SDK nor Sec_Eng_Trng_* are available, so we
 * provide a weak rand()-backed implementation here. A stronger HW TRNG
 * wrapper (e.g. bflb_sec_trng) can override this without touching the
 * upstream sources.
 */
#include <stdlib.h>

__attribute__((weak)) int bl_rand(void)
{
    return rand();
}
