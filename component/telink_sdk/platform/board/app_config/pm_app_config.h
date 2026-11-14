/********************************************************************************************************
 * @file    pm_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2019
 *
 * @par     Copyright (c) 2019, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

/**
 * @brief define 32k clock tick per us/ms/s.
 */
enum
{
    CLOCK_32K_TIMER_TICK_1S  = 32000,
    CLOCK_32K_XTAL_TIMER_TICK_1S = 32768,
};

#if defined(MCU_CORE_B91)
    #define PM_TICK_STIMER                  PM_TICK_STIMER_16M
    #define FLD_PD_ZB_EN                    PM_POWER_BASEBAND
#elif defined(MCU_CORE_B92)
    #define FLD_PD_ZB_EN                    PM_POWER_BASEBAND
#endif

#if (defined(MCU_CORE_B91) || defined(MCU_CORE_TL7518))
#define MCU_DEEPRET_BACK                    MCU_STATUS_DEEPRET_BACK
#endif


#define CURRENT_TEST                        1

#define WAKEUP_PAD                          GPIO_PA2
#define WAKEUP_CORE_PAD                     GPIO_PA1
#if defined(MCU_CORE_TL751X)
#define LPC_WAKEUP_PAD                      LPC_INPUT_PG1
#elif (defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X))
#define LPC_WAKEUP_PAD                      LPC_INPUT_PB5
#else
#define LPC_WAKEUP_PAD                      LPC_INPUT_PB1
#endif
#define CRC_OK                              1
#define MDEC_MATCH_VALUE                    0x02

#define QDEC_WAKEUP_PAD_CHAN_A              QDEC_CHN_PB6
#define QDEC_WAKEUP_PAD_CHAN_B              QDEC_CHN_PB7


/* PM_CLOCK_SELECT */
#define PM_CLK_32K_RC                       00
#if !defined(MCU_CORE_TL521X)
#define PM_CLK_32K_XTAL                     01
#endif
#define PM_CLOCK_SELECT                     PM_CLK_32K_RC


/* PM_WAKEUP_TICK_TYPE */
#define PM_WAKEUP_SLEEP_TYPE                00
#define PM_WAKEUP_LONG_SLEEP_TYPE           01
#define PM_WAKEUP_TICK_TYPE                 PM_WAKEUP_SLEEP_TYPE

#define PM_CORE_SINGLE                      1
#if defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL753X)
#define PM_CORE_MULTI                       2
#endif
#define PM_SET_DVDD_MODE                    3 // Switch voltage for crash test
#if defined(MCU_CORE_TL322X)
#define PM_2M_WFI_MODE                      4 // Set PM_WFI_OPTIMIZATION = 1
#endif
#define PM_AUDO_TEST                        5
#define PM_DEMO_MODE                        PM_CORE_SINGLE

#define N22_TEST                            0
#define DSP_TEST                            0
#define DSP_FW_DOWNLOAD_FLASH_ADDR          0x20040000
#define N22_FW_DOWNLOAD_FLASH_ADDR          0x20080000

/**
 * @note To enter sleep using COMPARATOR Wake mode, the voltage difference between the input level and the configured wake level
 *       needs to be greater than 100mV. If the input level is particularly close to the wake level, the chip will not sleep properly
 *       due to the unstable state of the LPC, resulting in a crash.
 */
/* PM_MODE */
#define PAD_WAKEUP                          (1<<0)
#define TIMER_WAKEUP                        (1<<1)
#define COMPARATOR_WAKEUP                   (1<<2)
#define CORE_USB_WAKEUP                     (1<<3)              //only supported suspend
#define CORE_GPIO_WAKEUP                    (1<<4)              //only supported suspend
#if (defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X))
#define SHUTDOWN_WAKEUP                     (1<<8)
#endif
#if defined(MCU_CORE_B91)
#define MDEC_WAKEUP                         (1<<5)
#define ALL_WAKEUP                          (PAD_WAKEUP | TIMER_WAKEUP | COMPARATOR_WAKEUP | CORE_USB_WAKEUP | CORE_GPIO_WAKEUP | MDEC_WAKEUP)
#elif defined(MCU_CORE_B92)
#define CTB_WAKEUP                          (1<<6) 
#define ALL_WAKEUP                          (PAD_WAKEUP | TIMER_WAKEUP | COMPARATOR_WAKEUP | CORE_USB_WAKEUP | CORE_GPIO_WAKEUP | CTB_WAKEUP)
#elif defined(MCU_CORE_TL751X)
#define WT_WAKEUP                           (1<<7) 
#define ALL_WAKEUP                          (PAD_WAKEUP | TIMER_WAKEUP | COMPARATOR_WAKEUP | CORE_USB_WAKEUP | CORE_GPIO_WAKEUP | WT_WAKEUP)
#elif (defined(MCU_CORE_TL523X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL322X))
#define CORE_QDEC_WAKEUP                    (1<<5)
#define ALL_WAKEUP                          (PAD_WAKEUP | TIMER_WAKEUP | COMPARATOR_WAKEUP | CORE_USB_WAKEUP | CORE_GPIO_WAKEUP | CORE_QDEC_WAKEUP)
#else
#define ALL_WAKEUP                          (PAD_WAKEUP | TIMER_WAKEUP | COMPARATOR_WAKEUP | CORE_USB_WAKEUP | CORE_GPIO_WAKEUP)
#endif

#define PM_MODE                             PAD_WAKEUP

/* PM_SLEEP_MODE */
#if defined(MCU_CORE_B91)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET64K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW64K

#elif (defined(MCU_CORE_B92) || defined(MCU_CORE_TL321X))
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET64K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW64K
#define DEEP_RET96K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW96K

#elif defined(MCU_CORE_TL322X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET64K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW64K
#define DEEP_RET128K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW128K
#define DEEP_RET256K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW256K
#define DEEP_RET384K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW384K

#elif defined(MCU_CORE_TL721X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET64K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW64K
#define DEEP_RET128K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW128K
#define DEEP_RET256K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW256K

#elif defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET96K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW96K
#define DEEP_RET160K_MODE                   DEEPSLEEP_MODE_RET_SRAM_LOW160K
#define PM_SHUTDOWN_MODE                    SHUTDOWN_MODE

#elif defined(MCU_CORE_TL751X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET128K_MODE                   RET_MODE_SRAM_LOW128K_NONE_NONE
#define DEEP_RET256K_MODE                   RET_MODE_SRAM_LOW256K_NONE_NONE
#define DEEP_RET384K_MODE                   RET_MODE_SRAM_LOW384K_NONE_NONE

#elif (defined(MCU_CORE_TL7518) || defined(MCU_CORE_W92))
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET128K_MODE                   D25F_RET_MODE_SRAM_LOW128K
#define DEEP_RET256K_MODE                   D25F_RET_MODE_SRAM_LOW256K
#define DEEP_RET384K_MODE                   D25F_RET_MODE_SRAM_LOW384K

#elif defined(MCU_CORE_TL753X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET64K_MODE                    RET_MODE_SRAM_LOW64K_NONE_NONE
#define DEEP_RET384K_MODE                   RET_MODE_SRAM_LOW384K_NONE_NONE

#elif defined(MCU_CORE_TL523X)
#define PM_SUSPEND_MODE                     SUSPEND_MODE
#define PM_DEEPSLEEP_MODE                   DEEPSLEEP_MODE
#define DEEP_RET16K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW16K
#define DEEP_RET32K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW32K
#define DEEP_RET64K_MODE                    DEEPSLEEP_MODE_RET_SRAM_LOW64K
#define PM_SHUTDOWN_MODE                    SHUTDOWN_MODE

#endif

#define PM_SLEEP_MODE                       PM_SUSPEND_MODE



/********************************************************************************************************
 *                                          user don't change                                           * 
 *******************************************************************************************************/
#if (!defined(MCU_CORE_TL521X) && (PM_CLOCK_SELECT == PM_CLK_32K_XTAL))
#if (defined(MCU_CORE_B91) || defined(MCU_CORE_B92))
#define PULL_WAKEUP_SRC_PD0                 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD1                 GPIO_PIN_UP_DOWN_FLOAT

#elif defined(MCU_CORE_TL721X)
#define PULL_WAKEUP_SRC_PD2                 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD3                 GPIO_PIN_UP_DOWN_FLOAT

#elif (defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X)) //|| defined(MCU_CORE_TL521X)
#define PULL_WAKEUP_SRC_PC2                 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC3                 GPIO_PIN_UP_DOWN_FLOAT
#endif
#endif

/* PM_SLEEP_WAKEUP_SRC */
#if (PM_MODE == PAD_WAKEUP)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_PAD
#elif (PM_MODE == TIMER_WAKEUP)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_TIMER
#elif (PM_MODE == COMPARATOR_WAKEUP)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_COMPARATOR
#elif (PM_MODE == CORE_USB_WAKEUP)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_CORE
#elif (PM_MODE == CORE_GPIO_WAKEUP)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_CORE
#elif ((defined(MCU_CORE_TL523X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL322X)) && (PM_MODE == CORE_QDEC_WAKEUP))
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_CORE
#elif (defined(MCU_CORE_B91) && (PM_MODE == MDEC_WAKEUP))
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_MDEC
#elif (defined(MCU_CORE_B92) && (PM_MODE == CTB_WAKEUP))
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_CTB
#elif (defined(MCU_CORE_TL751X) && (PM_MODE == WT_WAKEUP))
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_WT
#elif ((defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)) && (PM_MODE == SHUTDOWN_WAKEUP))
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_SHUTDOWN | PM_WAKEUP_PAD
#elif (PM_MODE == ALL_WAKEUP)
#if defined(MCU_CORE_B91)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_PAD | PM_WAKEUP_TIMER | PM_WAKEUP_COMPARATOR | PM_WAKEUP_CORE | PM_WAKEUP_MDEC
#elif defined(MCU_CORE_B92)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_PAD | PM_WAKEUP_TIMER | PM_WAKEUP_COMPARATOR | PM_WAKEUP_CORE | PM_WAKEUP_CTB
#elif defined(MCU_CORE_TL751X)
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_PAD | PM_WAKEUP_TIMER | PM_WAKEUP_COMPARATOR | PM_WAKEUP_CORE | PM_WAKEUP_WT
#else
#define PM_SLEEP_WAKEUP_SRC                 PM_WAKEUP_PAD | PM_WAKEUP_TIMER | PM_WAKEUP_COMPARATOR | PM_WAKEUP_CORE
#endif
#endif


#include "driver.h"
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
