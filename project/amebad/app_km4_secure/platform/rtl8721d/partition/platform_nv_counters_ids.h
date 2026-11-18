/*
 * RTL8721D (Ameba-D) TF-M platform NV-counter IDs.
 *
 * TF-M's platform/include/tfm_plat_nv_counters.h uses its built-in tfm_nv_counter_t
 * enum only when PLATFORM_DEFAULT_NV_COUNTERS is defined (our STORAGE=ON image, where
 * flash-backed NV counters back PS rollback); otherwise it falls to
 *   #include "platform_nv_counters_ids.h"
 * A platform must therefore provide this header for the no-storage build
 * (PLATFORM_DEFAULT_NV_COUNTERS OFF, e.g. make QUIC=ON TFM=ON): the Platform PSA-RoT
 * partition (TFM_PARTITION_PLATFORM=ON) compiles platform_sp.c, which pulls
 * tfm_plat_nv_counters.h and so needs the tfm_nv_counter_t type even though the module
 * is otherwise disabled. This lives in partition/ (already on platform_region_defs'
 * INTERFACE include path, so every SPE target that includes region_defs.h finds it).
 *
 * The enum mirrors TF-M's built-in set exactly so the two builds agree on the IDs.
 */
#ifndef __PLATFORM_NV_COUNTERS_IDS_H__
#define __PLATFORM_NV_COUNTERS_IDS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum tfm_nv_counter_t {
    PLAT_NV_COUNTER_PS_0 = 0,  /* Used by PS service */
    PLAT_NV_COUNTER_PS_1,      /* Used by PS service */
    PLAT_NV_COUNTER_PS_2,      /* Used by PS service */

    /* BL2 NV counters must be contiguous */
    PLAT_NV_COUNTER_BL2_0,     /* Used by bootloader */
    PLAT_NV_COUNTER_BL2_1,     /* Used by bootloader */
    PLAT_NV_COUNTER_BL2_2,     /* Used by bootloader */
    PLAT_NV_COUNTER_BL2_3,     /* Used by bootloader */

    PLAT_NV_COUNTER_BL1_0,     /* Used by bootloader */

    /* NS counters must be contiguous */
    PLAT_NV_COUNTER_NS_0,      /* Used by NS */
    PLAT_NV_COUNTER_NS_1,      /* Used by NS */
    PLAT_NV_COUNTER_NS_2,      /* Used by NS */

    PLAT_NV_COUNTER_MAX,
    PLAT_NV_COUNTER_BOUNDARY = UINT32_MAX  /* Fix tfm_nv_counter_t size to 4 bytes */
};

#ifdef __cplusplus
}
#endif

#endif /* __PLATFORM_NV_COUNTERS_IDS_H__ */
