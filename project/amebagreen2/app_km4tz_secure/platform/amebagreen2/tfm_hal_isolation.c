extern void LOGUART_PutChar(unsigned char c);
/* TF-M isolation level 1: SAU, locked MPC/PPC and checked S/NS buffers.
 * Secure partitions share their address space; L2/L3 MPU isolation is not enabled. */
#include "tfm_hal_isolation.h"
#include "array.h"
#include "cmsis.h"
#include "region_defs.h"
#include <arm_cmse.h>

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
    if (p_boundary != NULL) {
        *p_boundary = IS_NS_AGENT_TZ(p_ldinf) ? 1 : 0;
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
    if (size == 0) return TFM_HAL_SUCCESS;
    if (!base || size > UINTPTR_MAX - base) return TFM_HAL_ERROR_INVALID_INPUT;
    int flags;
    if ((access_type & TFM_HAL_ACCESS_READWRITE) == TFM_HAL_ACCESS_READWRITE)
        flags = CMSE_MPU_READWRITE;
    else if (access_type & TFM_HAL_ACCESS_READABLE) flags = CMSE_MPU_READ;
    else return TFM_HAL_ERROR_INVALID_INPUT;
    if ((access_type & TFM_HAL_ACCESS_NS) || boundary) {
        /* MPC protects SRAM aliases independently of the CPU's SAU result.
         * Reject those holes before the SPE could dereference an NS buffer. */
        uintptr_t end = base + size;
        int ram = (base >= 0x20001000 && end <= 0x20005000) ||
                  (base >= 0x2001b000 && end <= 0x200a0000) ||
                  (base >= 0x60000000 && end <= 0x60400000);
        int flash = !(access_type & TFM_HAL_ACCESS_WRITABLE) &&
                    ((base >= 0x08000000 && end <= 0x083f0000) ||
                     (base >= NS_CODE_START && end <= NS_CODE_LIMIT + 1));
        if (!ram && !flash) return TFM_HAL_ERROR_MEM_FAULT;
        flags |= CMSE_NONSECURE;
    }
    if ((access_type & TFM_HAL_ACCESS_UNPRIVILEGED) ||
        (boundary && (__TZ_get_CONTROL_NS() & 1))) flags |= CMSE_MPU_UNPRIV;
    if (cmse_check_address_range((void *)base, size, flags)) return TFM_HAL_SUCCESS;
    const char *msg = "[TF-M] memory reject ";
    while (*msg) LOGUART_PutChar(*msg++);
    uint32_t values[] = {boundary,base,size,access_type,(uint32_t)flags,cmse_TT((void *)base).value};
    for (unsigned j=0;j<6;++j) {
        for (int i=28;i>=0;i-=4) LOGUART_PutChar("0123456789abcdef"[(values[j]>>i)&15]);
        LOGUART_PutChar(' ');
    }
    LOGUART_PutChar('\n');
    return TFM_HAL_ERROR_MEM_FAULT;
}
