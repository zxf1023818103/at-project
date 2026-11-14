/********************************************************************************************************
 * @file    lpd.h
 *
 * @brief   This is the header file for tl323x
 *
 * @author  Driver Group
 * @date    2025
 *
 * @par     Copyright (c) 2025, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once

#include "lib/include/analog.h"
#include "lib/include/stimer.h"
#include "reg_include/analog_afe3v_reg.h"

/**
 * LPD power-down protection mode switch:
 * - 0: hardware trigger mode (default). The mspi and mcu resets are held by hardware, without software
 *      participation. This is the safest scheme and is recommended to keep as default.
 * - 1: interrupt mode.
 *      There are limitations and risks to using lpd interrupt mode:
 *      1) Risks:
 *       - In the interrupt mode, after LPD trigger, the hardware first holds the mspi reset, and then triggers
 *         the interrupt software to hold the mcu reset. Compared with the pure hardware scheme, the software
 *         scheme has hysteresis and the risk increases.
 *      2) Limitations:
 *       - Interrupt preemption must be enabled and the LPD interrupt will be set as the only highest priority
 *         interrupt that can interrupt any process.
 *       - LPD interrupt priority(IRQ_LPD) > flash operation priority > other interrupt priority.
 *       - core_interrupt_enable() must be called by the user, otherwise the function will not work
 *         and must be called as soon as possible, which is better for chip security.
 */
#define  LPD_IRQ_ENABLE   0

typedef enum
{
    LPD_FALLING_1P60V_RISING_1P70V,
    LPD_FALLING_1P70V_RISING_1P80V = 1,//default
    LPD_FALLING_1P79V_RISING_1P88V,
    LPD_FALLING_1P96V_RISING_2P06V,
    LPD_FALLING_2P10V_RISING_2P20V,
    LPD_FALLING_2P28V_RISING_2P38V,
    LPD_FALLING_2P37V_RISING_2P47V,
    LPD_FALLING_2P46V_RISING_2P56V,

    LPD_FALLING_1P98V_RISING_2P08V,
    LPD_FALLING_2P12V_RISING_2P22V,
    LPD_FALLING_2P27V_RISING_2P37V,
    LPD_FALLING_2P45V_RISING_2P55V,
    LPD_FALLING_2P59V_RISING_2P69V,
    LPD_FALLING_2P77V_RISING_2P87V,
    LPD_FALLING_2P86V_RISING_2P95V,
    LPD_FALLING_2P95V_RISING_3P04V,

} lpd_threshold_vol_e;

/**
 * @brief       This function enables the LPD analog module.
 * @return      none.
 * @note        The LPD analog configuration must be set before the LPD digital configuration.
 */
static _always_inline void lpd_ana_enable(void)
{
    g_areg_aon_14 &= (~FLD_PD_ACTIVE_LPD);
    analog_write_reg8(areg_aon_0x14, g_areg_aon_14);//lpd analog configuration must be set before lpd digital configuration, otherwise lpd may be triggered abnormally.
    g_areg_aon_14 |= FLD_RESET_SIGNAL;
    analog_write_reg8(areg_aon_0x14, g_areg_aon_14);//hold the analog lpd reset signal to prevent accidental triggering

}

/**
 * @brief       This function disables the LPD function.
 * @return      none.
 */
static _always_inline void lpd_disable(void)
{
    reg_pvd_config &= ~(FLD_LPD_FLASH_SEL | FLD_LPD_FLASH_EN | FLD_LPD_FLASH_RST_MCU);//lpd digital disable
    g_areg_aon_14 |= FLD_PD_ACTIVE_LPD;
    analog_write_reg8(areg_aon_0x14, g_areg_aon_14);
}

/**
 * @brief       This function sets the LPD interrupt mask.
 * @return      none.
 */
static inline void lpd_set_irq_mask(void)
{
    reg_pvd_config |= FLD_LPD_INT_MASK;
}

/**
 * @brief       This function gets the LPD interrupt status.
 * @return      LPD interrupt status.
 */
static inline unsigned short lpd_get_irq_status(void)
{
    return (reg_lpd_int_status & FLD_LPD_FLASH_STATUS);
}

/**
 * @brief       This function clears the LPD interrupt status.
 * @return      none.
 */
static inline void lpd_clr_irq_status(void)
{
    reg_lpd_int_status |= FLD_LPD_FLASH_STATUS;
}

/**
 * @brief       This function is used to select the MSPI or MSPI and MCU that is reset hold(not release) when LPD is triggered.
 * @param[in]   mcu_reset_hold_en -Whether to enable MCU reset hold. It is only effective in hardware trigger mode
 *              (LPD_IRQ_ENABLE == 0). In interrupt mode it is ignored, because the MCU reset is held by the LPD
 *              interrupt handler instead.
 * @return      none.
 * @note        -# lpd_trigger_reset_config_and_enable() must be called at least 100 us after lpd_ana_enable().
 *              Otherwise, the LPD may be erroneously triggered during power-up or wake-up from sleep, causing the chip to crash.
 *
 *              There are limitations and risks to using lpd interrupts mode:
 *              1) Risks:
 *               -# In the interrupt mode, after LPD trigger, the hardware first holds the mspi reset, and then triggers the interrupt software to
 *                  hold the mcu reset. Compared with the pure hardware scheme, the software scheme has hysteresis and the risk increases.
 *              2) Limitations:
 *              -# Interrupt preemption must be enabled and the LPD interrupt will be set as the only highest priority interrupt that can interrupt any process.
 *              -# LPD interrupt priority(IRQ_LPD) > flash operation priority > other interrupt priority.
 *              -# core_interrupt_enable() must be called by the user, otherwise the function will not work
 *                 and must be called as soon as possible, which is better for chip security.
 */
static _always_inline void lpd_trigger_reset_config_and_enable(unsigned char mcu_reset_hold_en)
{
#if LPD_IRQ_ENABLE
    (void)mcu_reset_hold_en;
    /**
     * The flash preemption threshold defaults to 1 in flash.c (s_flash_preempt_config), so setting the LPD
     * priority to the highest level ensures that the LPD interrupt can interrupt flash operations when flash
     * preemption is enabled by flash_plic_preempt_config().
     */
    plic_set_priority(IRQ_LPD, IRQ_PRI_LEV3);//Setting the interrupt priority of LPD to highest IRQ_PRI_LEV3.
    plic_preempt_feature_en(CORE_PREEMPT_PRI_MODE0);//enable preemptive priority interrupt feature.

    reg_pvd_config = (reg_pvd_config & ~(FLD_LPD_FLASH_SEL | FLD_LPD_FLASH_POL | FLD_LPD_FLASH_RST_MCU)) | FLD_LPD_FLASH_SEL;//mspi reset must be selected, only mspi reset hold by hardware, not release
    analog_write_reg8(areg_aon_0x14, (analog_read_reg8(areg_aon_0x14) & (~FLD_RESET_SIGNAL)));//release the analog lpd reset signal
    reg_lpd_int_status |= FLD_LPD_FLASH_STATUS;//clear lpd irq status to prevent accidental triggering
    reg_pvd_config = reg_pvd_config | FLD_LPD_FLASH_EN;//lpd digital enable
    lpd_set_irq_mask();
    plic_interrupt_enable(IRQ_LPD);

#else

    if(mcu_reset_hold_en)
    {
        reg_pvd_config = (reg_pvd_config & ~(FLD_LPD_FLASH_SEL | FLD_LPD_FLASH_POL | FLD_LPD_FLASH_RST_MCU)) | FLD_LPD_FLASH_SEL | FLD_LPD_FLASH_RST_MCU;//mspi and mcu reset hold by hardware, not release
    } else {
        reg_pvd_config = (reg_pvd_config & ~(FLD_LPD_FLASH_SEL | FLD_LPD_FLASH_POL | FLD_LPD_FLASH_RST_MCU)) | FLD_LPD_FLASH_SEL;//mspi reset must be selected, only mspi reset hold by hardware, not release
    }
    g_areg_aon_14 &= (~FLD_RESET_SIGNAL);
    analog_write_reg8(areg_aon_0x14, g_areg_aon_14);//release the analog lpd reset signal
    reg_lpd_int_status |= FLD_LPD_FLASH_STATUS;//clear lpd irq status to prevent accidental triggering
    reg_pvd_config = reg_pvd_config | FLD_LPD_FLASH_EN;//lpd digital enable
#endif
}

/**
 * @brief       This function used for setting the LPD vbat threshold
 * @return      none.
 * @note       -# A0 is not calibrated, so the register value is set to the default position of 1. 
 *              For future versions including A1, the g_lpd_vbat_efuse_code is read from the efuse, looked up in the lpd_vbat_1p7_trim_tab table, and the corresponding register value is set.
 */
_attribute_ram_code_sec_noinline_ void lpd_set_vbat_threshold(lpd_threshold_vol_e thres_vol);

/**
 * @brief       This function serves to protect the flash during the chip power-down process.
 * @return      none.
 * @note       -# In order to improve the robustness of the chip during high-speed operation, the low power detection (LPD) is used to
 *              protect the flash during power-down of the chip .
 *             -# This feature is enabled by default, and the chip power supply voltage is limited to 2.1V to 4.5V.
 */
_attribute_ram_code_sec_noinline_ void lpd_power_down_protect_ana_config(void);


/**
 * @brief       This function serves to protect the flash during the chip power-down process for deep retention wake up.
 * @return      none.
 * @note       -# In order to improve the robustness of the chip during high-speed operation, the low power detection (LPD) is used to
 *              protect the flash during power-down of the chip .
 *             -# This feature is enabled by default, and the chip power supply voltage is limited to 2.1V to 4.5V.
 */
void lpd_power_down_protect_ana_config_for_deep_ret(void);
