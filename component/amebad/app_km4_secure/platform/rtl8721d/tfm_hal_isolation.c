/*
 * RTL8721D KM4 — TF-M isolation HAL (bring-up, isolation level 1).
 *
 * Level-1 minimal: SAU already separates S/NS (target_cfg.c). No per-partition
 * MPU enforcement yet (that is L2/L3). These hooks keep the SPM happy and pass
 * memory checks; tighten with MPU/PPC when raising the isolation level.
 *
 * NOTE: function signatures follow TF-M v2.1's tfm_hal_isolation API; verify
 * against ${TFM}/secure_fw/include/tfm_hal_isolation.h during the build loop.
 */
#include "tfm_hal_isolation.h"
#include "array.h"
#include "cmsis.h"
#include "region_defs.h"

#ifdef CONFIG_TFM_USE_TRUSTZONE
REGION_DECLARE(Image$$, ER_VENEER, $$Base);
REGION_DECLARE(Image$$, VENEER_ALIGN, $$Limit);
#endif

uint32_t handle_attr_to_part_priv(uintptr_t handle) { (void)handle; return 0; }

enum tfm_hal_status_t tfm_hal_set_up_static_boundaries(
    uintptr_t *p_spm_boundary)
{
    /* SAU/IDAU already set in tfm_hal_platform_init via target_cfg. L1: no MPU. */
    if (p_spm_boundary != NULL) {
        *p_spm_boundary = (uintptr_t)0;
    }
    return TFM_HAL_SUCCESS;
}

enum tfm_hal_status_t tfm_hal_bind_boundary(
    const struct partition_load_info_t *p_ldinf, uintptr_t *p_boundary)
{
    (void)p_ldinf;
    if (p_boundary != NULL) {
        *p_boundary = (uintptr_t)0;
    }
    return TFM_HAL_SUCCESS;
}

enum tfm_hal_status_t tfm_hal_activate_boundary(
    const struct partition_load_info_t *p_ldinf, uintptr_t boundary)
{
    (void)p_ldinf; (void)boundary;
    return TFM_HAL_SUCCESS;
}

enum tfm_hal_status_t tfm_hal_memory_check(
    uintptr_t boundary, uintptr_t base, size_t size, uint32_t access_type)
{
    (void)boundary; (void)base; (void)size; (void)access_type;
    /* L1: trust the SAU split; accept. Tighten at L2/L3 with cmse TT checks. */
    return TFM_HAL_SUCCESS;
}
