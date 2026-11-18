/* Green2 has separate flash, SRAM and PSRAM MPCs, with six regions each. */
#include "ameba_soc.h"
#include "ameba_secure_boot.h"
const SAU_CFG_TypeDef sau_config[SAU_ENTRY_NUM] = {
    {0x0001f000, 0x001fffff, 0},
    {0x08000000, 0x083effff, 0}, /* top 64 KiB belongs to secure storage */
    {0x40000000, 0x4011cfff, 0}, /* SPIC programming aperture stays secure */
    {0x04000000, 0x0400001f, 0}, /* limit replaced by actual AP XIP length */
    {0x20001000, 0x2009ffff, 0}, /* MPC excludes secure SRAM and its alias */
    {0x4011e000, 0x4fffffff, 0},
    {0x60000000, 0x603fffff, 0},
    {0x0c000800, 0x0c000fff, 1}, /* NSC uses bit-28-clear alias; 0x1... is always secure */
};
u32 green2_km4ns_xip_end, green2_km4tz_xip_end, green2_active_bank;
void green2_tfm_configure_aliases(void)
{
    SAU->CTRL = 0;
    for (unsigned i = 0; i < SAU_ENTRY_NUM; ++i) {
        u32 end = i == 3 ? green2_km4tz_xip_end : sau_config[i].End;
        SAU->RNR = i;
        SAU->RBAR = sau_config[i].Start & SAU_RBAR_BADDR_Msk;
        SAU->RLAR = (end & SAU_RLAR_LADDR_Msk) | SAU_RLAR_ENABLE_Msk |
                    (sau_config[i].NSC ? SAU_RLAR_NSC_Msk : 0);
    }
    SAU->CTRL = SAU_CTRL_ENABLE_Msk;
    __DSB(); __ISB();
}
#define END_REGION {0xffffffff, 0xffffffff, MPC_RW | MPC_NS}
const TZ_CFG_TypeDef mpc1_config[MPC_ENTRY_NUM] = {
    {0x02000000, 0x0200001f, MPC_RO | MPC_NS},
    {0x04000000, 0x0400001f, MPC_RO | MPC_NS},
    {0x08000000, 0x083effff, MPC_RO | MPC_NS},
    {0x0c000000, 0x0c000fff, MPC_RO | MPC_NS}, /* SAU narrows this to the SG veneers */
    END_REGION, END_REGION,
};
const TZ_CFG_TypeDef mpc2_config[MPC_ENTRY_NUM] = {
    {0x20001000, 0x20004fff, MPC_RW | MPC_NS},
    {0x2001b000, 0x2009ffff, MPC_RW | MPC_NS},
    END_REGION, END_REGION, END_REGION, END_REGION,
};
const TZ_CFG_TypeDef mpc3_config[MPC_ENTRY_NUM] = {
    {0x60000000, 0x603fffff, MPC_RW | MPC_NS},
    END_REGION, END_REGION, END_REGION, END_REGION, END_REGION,
};
fih_ret green2_tfm_boot_tzcfg(void)
{
    MPC_TypeDef *mpc[] = { AP_MPC1, AP_MPC2, AP_MPC3 };
    const TZ_CFG_TypeDef *cfg[] = { mpc1_config, mpc2_config, mpc3_config };
    DCache_CleanInvalidate(0xffffffff, 0xffffffff);
    for (unsigned n = 0; n < 3; ++n) {
        u32 ctrl = 0;
        for (unsigned i = 0; i < MPC_ENTRY_NUM; ++i) {
            volatile u32 *entry = (volatile u32 *)&mpc[n]->MPC_Entry[i];
            if (cfg[n][i].Start == 0xffffffff) break;
            u32 end = cfg[n][i].End;
            if (n == 0 && i == 0) end = green2_km4ns_xip_end;
            if (n == 0 && i == 1) end = green2_km4tz_xip_end;
            entry[0] = cfg[n][i].Start & RXI300_MASK_IDAU_BAx;
            entry[1] = end & RXI300_MASK_IDAU_LAx;
            ctrl |= (cfg[n][i].CTRL & MPC_NS) ? 1u << i : 0;
            ctrl |= (cfg[n][i].CTRL & MPC_RO) ? 1u << (i + 16) : 0;
        }
        mpc[n]->IDAU_CTRL = ctrl;
        mpc[n]->IDAU_LOCK = 1;
    }
    AP_PPC->PPC1_REG = 0xffffffff;
    AP_PPC->PPC2_REG = 0xffffffff;
    AP_PPC->PPC3_REG = 0xffffffff & ~(1u << 2);
    green2_tfm_configure_aliases();
    SCB->SCR |= SCB_SCR_SLEEPDEEPS_Msk;
    SCB->AIRCR = (0x5fa << SCB_AIRCR_VECTKEY_Pos) |
                 (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_PRIS_Msk;
    for (unsigned i = 0; i < 3; ++i) NVIC->ITNS[i] = 0xffffffff;
    __DSB(); __ISB();
    cmse_address_info_t stack = cmse_TT((void *)0x20002fe0);
    cmse_address_info_t secure = cmse_TT((void *)0x3000b000);
    RTK_LOGI("TFM", "NS stack TT=%08lx secure SRAM TT=%08lx\n", stack.value, secure.value);
    if (!stack.flags.nonsecure_readwrite_ok || stack.flags.secure || !secure.flags.secure)
        FIH_RET(FIH_FAILURE);
    FIH_RET(FIH_SUCCESS);
}
