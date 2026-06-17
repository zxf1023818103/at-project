#ifndef BFLB_MM_PORT_BFLB_L1C_H
#define BFLB_MM_PORT_BFLB_L1C_H
/*
 * Portable shim of the Bouffalo SDK's <bflb_l1c.h> for non-Bouffalo builds of the
 * shared tlsf_config. mm.h reads only BFLB_CACHE_LINE_SIZE (to size MM_FLAG_ALIGN_CACHE
 * for DMA-coherent allocations). 32 is correct for AmebaD KM4 (Cortex-M33) and
 * harmless for KM0 (no data cache).
 *
 * mm.h is the FIRST header that needs IS_ENABLED (in its struct definitions), and
 * it #includes <bflb_l1c.h> before using it, but does NOT include <bflb_core.h>.
 * On Bouffalo IS_ENABLED comes from the global bflb_autoconf; for portable builds
 * we define it here so it is available to mm.h and every consumer that includes it.
 */
#define BFLB_CACHE_LINE_SIZE 32

#ifndef IS_ENABLED
/* Linux-kernel style: 1 iff the macro is defined to 1; 0 for 0 or undefined. */
#define IS_ENABLED(option)        __MM_IS_ENABLED1(option)
#define __MM_IS_ENABLED1(option)  __MM_IS_ENABLED2(__MM_ARG_PLACEHOLDER_##option)
#define __MM_ARG_PLACEHOLDER_1    0,
#define __MM_IS_ENABLED2(x)       __MM_IS_ENABLED3(x 1, 0)
#define __MM_IS_ENABLED3(ig, val, ...) val
#endif

#endif /* BFLB_MM_PORT_BFLB_L1C_H */
