/* Dplus static secure/nonsecure boundary. TF-M owns 64 KiB SRAM and XIP entry 3. */
#include "ameba_soc.h"
const SAU_CFG_TypeDef sau_config[SAU_ENTRY_NUM] = {
    {0x0001e000, 0x000447ff, 0},
    {0x08000000, 0x083effff, 0}, /* top 64 KiB: PSA storage, secure only */
    {0x40000000, 0x40127fff, 0}, /* keep the SPIC programming aperture secure */
    {0x0e000000, 0x0e00001f, 0},
    {0x20000000, 0x200fffff, 0}, /* MPC excludes secure SRAM */
    {0x40129000, 0x4fffffff, 0},
    {0x60000000, 0x603fffff, 0},
    {0x0f000800, 0x0f000fff, 1}, /* SG veneers only */
};
u32 dplus_km0_xip_end, dplus_km4_xip_end, dplus_active_bank;
void dplus_tfm_configure_aliases(void)
{
    SAU->CTRL = 0;
    for (unsigned i = 0; i < SAU_ENTRY_NUM; ++i) {
        u32 end = i == 3 ? dplus_km4_xip_end : sau_config[i].End;
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
    {0x20000000, 0x20006fff, MPC_RW | MPC_NS},
    {0x20008000, 0x20008fff, MPC_RW | MPC_NS},
    {0x2001b000, 0x200fffff, MPC_RW | MPC_NS},
    END_REGION, END_REGION, END_REGION, END_REGION, END_REGION,
};
const TZ_CFG_TypeDef mpc2_config[MPC_ENTRY_NUM] = {
    {0x60000000, 0x603fffff, MPC_RW | MPC_NS},
    END_REGION, END_REGION, END_REGION, END_REGION, END_REGION, END_REGION, END_REGION,
};

/* TF-M needs the same attribution on development and provisioned devices.
 * The SDK's BOOT_RAM_TZCfg chooses a different table on some chip revisions.
 * Configure the documented MPC/PPC registers explicitly; no OTP is changed. */
void dplus_tfm_boot_tzcfg(void)
{
    MPC_TypeDef *mpc[] = { KM4_MPC1, KM4_MPC2 };
    const TZ_CFG_TypeDef *cfg[] = { mpc1_config, mpc2_config };
    DCache_CleanInvalidate(0xffffffff, 0xffffffff);
    for (unsigned n = 0; n < 2; ++n) {
        u32 ctrl = 0;
        for (unsigned i = 0; i < MPC_ENTRY_NUM; ++i) {
            volatile u32 *entry = (volatile u32 *)&mpc[n]->MPC_Entry[i];
            if (cfg[n][i].Start == 0xffffffff) break;
            entry[0] = cfg[n][i].Start & 0x0fffffff;
            entry[1] = cfg[n][i].End & 0x0fffffff;
            ctrl |= (cfg[n][i].CTRL & MPC_NS) ? 1u << i : 0;
            ctrl |= (cfg[n][i].CTRL & MPC_RO) ? 1u << (i + 16) : 0;
        }
        mpc[n]->IDAU_CTRL = ctrl;
        mpc[n]->IDAU_LOCK = 1;
    }
    KM4_PPC->PPC1_REG = 0xffffffff;
    KM4_PPC->PPC2_REG = 0xffffffff;
    KM4_PPC->PPC3_REG = 0xffffffff & ~(1u << 2); /* attribution controller */
    /* Keep PPC unlocked until SPE has finished initializing its services. */
    HAL_WRITE32(0x50110000, 0x398, 1);
    dplus_tfm_configure_aliases();
    SCB->SCR |= SCB_SCR_SLEEPDEEPS_Msk;
    SCB->AIRCR = (0x5fa << SCB_AIRCR_VECTKEY_Pos) |
                 (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_PRIS_Msk;
    for (unsigned i = 0; i < 3; ++i) NVIC->ITNS[i] = 0xffffffff;
    __DSB(); __ISB();
    cmse_address_info_t stack = cmse_TT((void *)0x20003fe0);
    cmse_address_info_t secure = cmse_TT((void *)0x3000b000);
    RTK_LOGI("TFM", "NS stack TT=%08lx secure SRAM TT=%08lx\n", stack.value, secure.value);
    assert_param(stack.flags.nonsecure_readwrite_ok && !stack.flags.secure);
    assert_param(secure.flags.secure);
}
