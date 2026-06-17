#ifndef BFLB_MM_PORT_BFLB_CORE_H
#define BFLB_MM_PORT_BFLB_CORE_H
/*
 * Portable shim of the Bouffalo SDK's <bflb_core.h>, used when the shared tlsf_config
 * component is built for a non-Bouffalo platform (the real header is on the path
 * only when bflb_lhal is linked). mm.c uses exactly two things from bflb_core.h:
 *
 *   - IS_ENABLED(CONFIG_x): true iff CONFIG_x is #defined to 1 (else 0/undefined).
 *   - bflb_irq_save()/bflb_irq_restore(): the critical section used by the WEAK
 *     mm_lock_save()/mm_unlock_restore() defaults. Each non-Bouffalo platform is
 *     expected to provide a STRONG mm_lock_save/mm_unlock_restore (a real critical
 *     section), so these no-op fallbacks only need to compile + link.
 */
#include <stdint.h>

#ifndef IS_ENABLED
/* Linux-kernel style: 1 iff the macro is defined to 1; 0 for 0 or undefined. */
#define IS_ENABLED(option)        __MM_IS_ENABLED1(option)
#define __MM_IS_ENABLED1(option)  __MM_IS_ENABLED2(__MM_ARG_PLACEHOLDER_##option)
#define __MM_ARG_PLACEHOLDER_1    0,
#define __MM_IS_ENABLED2(x)       __MM_IS_ENABLED3(x 1, 0)
#define __MM_IS_ENABLED3(ig, val, ...) val
#endif

static inline uintptr_t bflb_irq_save(void)      { return 0; }
static inline void      bflb_irq_restore(uintptr_t flags) { (void)flags; }

#endif /* BFLB_MM_PORT_BFLB_CORE_H */
