/********************************************************************************************************
 * @file    rf_app_config.h
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
#include "driver.h"
#define GPIO_TOGGLE     1

#define RF_BLE_1M       1
#define RF_BLE_2M       2
#define RF_BLE_1M_NO_PN 3
#define RF_BLE_2M_NO_PN 5

#if !defined(MCU_CORE_TL753X)
#define RF_ZIGBEE_250K  4 //TODO:TL7518 is temporarily unavailable, available versions will be updated in the future
#define RF_LR_S2_500K   6
#define RF_LR_S8_125K   7
    #if !defined(MCU_CORE_TL7518)
    #define RF_PRIVATE_250K 8
    #define RF_PRIVATE_500K 9
    #define RF_ANT          12 //TODO:TL721X Currently only validated in FPGA, not in chip; available after subsequent validation
#define RF_HYBEE_1M     13 //TODO:TL322X Currently only vlidated in FPGA, not in chip; available after subsequent validation
    #define RF_HYBEE_2M     14 //TODO:TL322X Currently only validated in FPGA, not in chip; available after subsequent validation
    #define RF_HYBEE_500K   15 //TODO:TL322X Currently only validated in FPGA, not in chip; available after subsequent validation
    #endif
#define RF_PRIVATE_1M   10 //TODO:TL7518 is temporarily unavailable, available versions will be updated in the future
#define RF_PRIVATE_2M   11 //TODO:TL7518 is temporarily unavailable, available versions will be updated in the future
#endif
#define RF_BLE_STX2RX   16
#define RF_BLE_SDK_TEST 19


#if defined(MCU_CORE_B92)
    #define RF_USER_DEFINE_PKT 17
#endif
#if defined(MCU_CORE_B92) || defined(MCU_CORE_TL721X)
    #define RF_BLE_1M_HYPER_LENGTH 18 //TODO:TL721X  Currently only validated in FPGA, not in chip; available after subsequent validation
#endif

#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)
//TODO:TL322X Currently only validated in FPGA, not in chip; available after subsequent validation
    #define RF_PRI_GENERIC_1M   22
    #define RF_PRI_GENERIC_2M   23
    #define RF_PRI_GENERIC_250K 24
    #define RF_PRI_GENERIC_500K 25
#endif

#if defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL521X)
    #define RF_BLE_4M   26//TODO:TL322X Currently only validated in FPGA, not in chip; available after subsequent validation
    #define RF_BLE_4M_NO_PN 28//TODO:TL322X Currently only validated in FPGA, not in chip; available after subsequent validation
        #if defined(MCU_CORE_TL322X)
    #define RF_BLE_6M   27
    #define RF_BLE_6M_NO_PN 29
        #endif
#endif

#define RF_FAST_SETTLE_TEST 41
#define TEST_BLE_AES_CCM    42

#define RF_MODE             RF_BLE_2M_NO_PN

#if (defined(MCU_CORE_TL322X))||(defined(MCU_CORE_TL753X))
#define RF_HIGH_PER_MODE     1
#endif

#if defined(MCU_CORE_TL721X)
    //The following operation is used only when CAL_0P94V_TO_0P95V is selected and an energy slot with the VANT keyword is used.
    #define rf_set_vant1p05_power_trim_vol_up()                  \
        do {                                                     \
            pm_cal_0p94v_e pm_trim_vol = pm_get_vdd0p94_level(); \
            if (((RF_POWER == RF_1P05_VANT_POWER_P4p30dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P4p20dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P3p80dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P3p30dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P2p85dBm)) &&   \
                (pm_trim_vol == CAL_0P94V_TO_0P95V)) {           \
                pm_set_vdd0p94(CAL_0P94V_TO_1P05V);              \
            }                                                    \
        } while (0)
    //The following operation is used only when CAL_0P94V_TO_0P95V is selected and an energy slot with the VANT keyword is used.
    #define rf_set_vant1p05_power_trim_vol_down()                \
        do {                                                     \
            pm_cal_0p94v_e pm_trim_vol = pm_get_vdd0p94_level(); \
            if (((RF_POWER == RF_1P05_VANT_POWER_P4p30dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P4p20dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P3p80dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P3p30dBm) ||    \
                 (RF_POWER == RF_1P05_VANT_POWER_P2p85dBm)) &&   \
                (pm_trim_vol == CAL_0P94V_TO_1P05V)) {           \
                pm_set_vdd0p94(CAL_0P94V_TO_0P95V);              \
            }                                                    \
        } while (0)
#elif defined(MCU_CORE_TL322X)

  #define rf_set_vant1p05_power_trim_vol_up()            \
        do{                                              \
            if (((RF_POWER == RF_VANT_POWER_P6p40) ||    \
                 (RF_POWER == RF_VANT_POWER_P6p00) ||    \
                 (RF_POWER == RF_VANT_POWER_P5p50) ||    \
                 (RF_POWER == RF_VANT_POWER_P5p00))){    \
                rf_set_vant_power_trim_level(RF_VANT_HIGH_POWER); \
            }                                                     \
        }while(0)

#define rf_set_vant1p05_power_trim_vol_down()            \
      do{                                              \
          if (((RF_POWER == RF_VANT_POWER_P6p40) ||    \
                 (RF_POWER == RF_VANT_POWER_P6p00) ||    \
                 (RF_POWER == RF_VANT_POWER_P5p50) ||    \
                 (RF_POWER == RF_VANT_POWER_P5p00))){    \
              rf_set_vant_power_trim_level(RF_VANT_NORMAL_POWER); \
          }                                                     \
      }while(0)
#else
    #define rf_set_vant1p05_power_trim_vol_up() \
        do {                                    \
        } while (0)
    #define rf_set_vant1p05_power_trim_vol_down() \
        do {                                      \
        } while (0)
#endif

#if defined(MCU_CORE_TL721X)
    #define rf_stimer_get_tick()     rf_bb_timer_get_tick()
    #define RF_SYSTEM_TIMER_TICK_1US BB_TIMER_TICK_1US
    #define RF_SYSTEM_TIMER_TICK_1MS BB_TIMER_TICK_1MS

#else
    #define rf_stimer_get_tick()     stimer_get_tick()
    #define RF_SYSTEM_TIMER_TICK_1US SYSTEM_TIMER_TICK_1US
    #define RF_SYSTEM_TIMER_TICK_1MS SYSTEM_TIMER_TICK_1MS
#endif


#if defined(MCU_CORE_B91)
    #define RF_POWER RF_POWER_P4p35dBm
#elif defined(MCU_CORE_B92)
    #define RF_POWER RF_POWER_P5p21dBm
#elif defined(MCU_CORE_TL7518)
    #define RF_POWER RF_POWER_P4p92dBm
#elif defined(MCU_CORE_TL721X)
    #define RF_POWER RF_POWER_P4p79dBm
#elif defined(MCU_CORE_TL321X)
    #define RF_POWER RF_POWER_P4p52dBm
#elif defined(MCU_CORE_TL323X)
    #define RF_POWER RF_POWER_P4p50dBm
#elif defined(MCU_CORE_TL322X)
    #define RF_POWER RF_POWER_P4p50dBm
#elif defined(MCU_CORE_TL751X)
    #define RF_POWER RF_POWER_P10p00dBm
#elif defined(MCU_CORE_TL752X)
    #define RF_POWER RF_POWER_LEVEL_20
#elif defined(MCU_CORE_TL753X)
    #define RF_POWER RF_POWER_P10p00dBm
#elif defined(MCU_CORE_TL521X)
    #define RF_POWER RF_POWER_P10p00dBm
#elif defined(MCU_CORE_TL523X)
    #define RF_POWER RF_POWER_P4p50dBm
#elif defined(MCU_CORE_TL522X)
    #define RF_POWER RF_POWER_P10p00dBm
#endif

#if(!defined(MCU_CORE_TL752X))
#include "driver.h"
#endif
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
