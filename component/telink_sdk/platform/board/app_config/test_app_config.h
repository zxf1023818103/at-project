/********************************************************************************************************
 * @file    test_app_config.h
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


#define TEST_PPM_MODE               1
#define TEST_FLASH_MODE             2
#define TEST_PM_MODE                3
#define TEST_AES_MODE               4
#define TEST_WATCHDOG_MODE          5
#define TEST_CHARGE_MODE            6
#define TEST_RF_CURRENT_MODE        7
#define TEST_ADC_VOLTAGE_MODE       8
#define TEST_ADC_TEMP_SENSOR_MODE   9
#define TEST_LPC_MODE               10
#define TEST_JAGUAR_CHARGE_MODE     11
#define TEST_EAGLE_CHARGER_MODE     12
#define TEST_DONGLE_LED_KEY_MODE    13
#define SLEEP_CLOCK_TEST            14
#define RC_32K_TEST                 15
#define FLASH_QUALITY_TEST          16
#define TEST_SLEEP_CURRENT_MODE     17
#define FLASH_WORK_VOLTAGE_TEST     18

/*
 * This demo use to trim puya to 2.1V reset.Because the trim setting need to power off flash to make the trim setting effect
 * after trim done.So in this demo,we provide the flash power on/off function.
 */
#define PUYA_FLASH_TRIM             19

/*
 * This demo is used to test whether the analog pull-up and pull-down can be maintained after wake-up from sleep,
 * because some pin corners of some chips cannot be maintained after wake-up from deep retention sleep.
 * The test method is to set all pin corners to analog pull-up and pull-down, and then capture the level state of all pin corners after sleep wake-up,
 * and see if the upper and lower pin corners cannot be maintained after sleep wake-up.
 */
#define TEST_ALL_PIN_PULLDOWN_UP    20

#define PUYA_FLASH_SCRATCH          21
#define TEST_EFUSE_TEST             22
#define TEST_CLOCK_CCLK_TEST        23

#if defined(MCU_CORE_B91)
#define TEST_HTOL_B91_MODE          24
/*
* This demo is given to the test group to test the eagle's keys, led, usb and audio functions.
* the bin file's addr is http://180.166.64.115:100/prj/release_bin_file/drivers_tools/Drivers/eagle/USB_AUDIO_KEY/bin_20230804_usb_audio_led_key_test
* after the chip is powered on and initialized, different key presses correspond to different led states.
* usb enumeration after usb connection to computer.
* switching jumper caps enables amic audio and line_in audio functionality 
*/
#define TEST_USB_LED_KEY_AUDIO      25
#define AUDIO_RELATED_TEST          26
#endif
#if defined(MCU_CORE_B92)
#define TEST_HTOL_B92_MODE          27
#define TEST_I2C_MODE               28
#define MULTIPLE_DMA_TEST           29
#define TEST_CODEC_0581_TEST        30
#define TEST_CTB_MODE               31
#define TEST_JTAG_MODE              32
#define TEST_AUDIO_RELATED          33
#define TEST_USB_LED_KEY_AUDIO      34
#endif
//#if defined(MCU_CORE_B92)||defined(MCU_CORE_TL721X)||defined(MCU_CORE_TL7518)||defined(MCU_CORE_TL321X)||defined(MCU_CORE_TL751X)
#define TEST_DCORE_TEST             35
//#endif
#if defined(MCU_CORE_TL721X)
#define TEST_HTOL_TL721X_MODE       36
#endif
#if defined(MCU_CORE_TL321X)
#define TEST_GPIO_PEM_MODE          37
#define TEST_HTOL_TL321X_MODE       38
#endif

#if !defined(MCU_CORE_B91)&&!defined(MCU_CORE_B92)
#define TEST_FLASH_TEST             39 //svn\release_bin_file\drivers_tools\Internal_Document\flash\FPGA
#define PSRAM_OSPI_TEST             40 //svn\release_bin_file\drivers_tools\Internal_Document\flash\FPGA_PSRAM
#define PSRAM_QSPI_TEST             41 //svn\release_bin_file\drivers_tools\Internal_Document\flash\FPGA_PSRAM
#endif

#define TEST_SECURE_BOOT            42

#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL753X)
#define TEST_OTP_TEST               43
#endif

#define TEST_SLEEP_TIME_ACCURACY    44

#if defined(MCU_CORE_TL7518)
#define TEST_AUDIO_ANC_TEST         45 /* audio anc test. */
#define TEST_AUDIO_ASRC_TEST        46 /* audio asrc test. */
#define TEST_AUDIO_RESAMPLE_TEST    47 /* audio resample test. */
#endif

#define TEST_CLOCK_XTAL_TEST        48
#if (defined(MCU_CORE_TL721X)||defined(MCU_CORE_TL321X))
#define TEST_KEY_LED_TEST           49
#endif

#if defined(MCU_CORE_TL751X)
#define TEST_HTOL_TL751X_MODE       50
#define TEST_HTOL_TL751X_N22_MODE   51
#define TEST_AUDIO_ANC_TEST         52
#define TEST_AUDIO_SPDIF_TEST       53
#endif

#if defined(MCU_CORE_TL321X)
#define TEST_CACHE                  54
#define TEST_ADC_MODE               55
#endif


#if defined(MCU_CORE_TL322X)
#define TEST_HTOL_TL322X_MODE       56
#define TEST_HTOL_TL322X_N22_MODE   57
#endif

#define REBOOT_TEST                 58

#define NVM_QUALITY_TEST           59
#define NVM_WORK_VOLTAGE_TEST      60
#define NVM_SLEEP_CURRENT_MODE     61
#define NVM_POWER_ONOFF_TEST       62
#define NVM_READ_TEST              63
#define NVM_LIFE_2_TEST            64
#define NVM_LIFE_3_TEST            65

#define TEST_HTOL_TL322X_CRC_MODE   66
#define PM_ZB_AUDIO_USB_TEST        67

#if defined(MCU_CORE_TL753X)
#define TEST_HTOL_TL753X_MODE       68
#define TEST_HTOL_TL753X_N22_MODE   69
#endif

#define FLASH_10W_TIME_TEST         70

#if defined(MCU_CORE_TL323X)
#define TEST_HTOL_TL323X_MODE       71
#endif

#if defined(MCU_CORE_TL752X)
#define TEST_HTOL_TL752X_MODE       72
#define TEST_PM_ALL_PAD_WAKEUP      73
#define TEST_N22_MAIL_BOX           74
#endif

#if defined(MCU_CORE_TL322X)
#define NVM_LIFE_ATE_TEST           75
#endif

#define TEST_INIT_CUR               77

#define TEST_MODE                   TEST_HTOL_TL753X_MODE

#if defined(MCU_CORE_B91)
#define PM_TICK_STIMER          PM_TICK_STIMER_16M
#if(TEST_MODE == TEST_HTOL_MODE)
#include "../BT_AutoTest_Demo/BT/utility.h"
#include "../BT_AutoTest_Demo/BT/bch.h"
#include "../BT_AutoTest_Demo/BT/rf_bt.h"
#include "types.h"
#endif

#elif defined(MCU_CORE_TL7518)
#if(TEST_MODE == TEST_EFUSE_TEST)
#include <string.h>
#endif

#elif (defined(MCU_CORE_TL321X)||defined(MCU_CORE_TL322X))
#if(TEST_MODE == TEST_EFUSE_TEST)
#include <string.h>
#endif
#endif

#if defined(MCU_CORE_B91)
#if(TEST_MODE == TEST_DONGLE_LED_KEY_MODE)
volatile unsigned char KEY1;
volatile unsigned char KEY2;
#define LED1            GPIO_PB7
#define LED2            GPIO_PB5
#define LED3            GPIO_PB4
#define LED4            GPIO_PB1
#define LED5            GPIO_PE3
#define LED6            GPIO_PB0

#define SW1                 GPIO_PB2
#define SW2                 GPIO_PB3
#else
#define LED1                GPIO_PB4
#define LED2                GPIO_PB5
#define LED3                GPIO_PB6
#define LED4                GPIO_PB7

#define SW1                 GPIO_PA0
#define SW2                 GPIO_PA1
#endif

#elif defined(MCU_CORE_B92)

#define  PM_9528A_T_80PIN       1
#define  PM_9528B_104PIN        2 //9528B_V1.3
#define  PM_9529A               3
#define  PM_9223B               4
#define  PM_PIN_MODE            PM_9528A_T_80PIN

#define XTAL_32k_TEST           1
#define RC_32k_TEST             2
#define CLOCK_32K_TEST_MODE     RC_32k_TEST

#if(PM_PIN_MODE == PM_9528A_T_80PIN)
#if(CLOCK_32K_TEST_MODE == RC_32k_TEST) 
#define TEST_LED1               GPIO_PD0
#define TEST_LED2               GPIO_PD1
#endif
#define TEST_LED3               GPIO_PE6
#define TEST_LED4               GPIO_PE7
#elif(PM_PIN_MODE == PM_9528B_104PIN)
#define TEST_LED1               GPIO_PE6
#define TEST_LED2               GPIO_PE7
#define TEST_LED3               GPIO_PF0
#define TEST_LED4               GPIO_PF1
#elif(PM_PIN_MODE == PM_9529A)
#define TEST_LED1               GPIO_PD3
#define TEST_LED2               GPIO_PD4
#define TEST_LED3               GPIO_PD5
#define TEST_LED4               GPIO_PD6
#elif(PM_PIN_MODE == PM_9223B)
#define TEST_LED1               GPIO_PD1
#define TEST_LED2               GPIO_PD1
#define TEST_LED3               GPIO_PD1
#define TEST_LED4               GPIO_PD1
#endif

#elif defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL323X)

#define TEST_LED1               GPIO_PC0
#define TEST_LED2               GPIO_PC1
#define TEST_LED3               GPIO_PC2
#define TEST_LED4               GPIO_PC3

#elif defined(MCU_CORE_TL321X)

#define TEST_LED1               GPIO_PD0
#define TEST_LED2               GPIO_PD1
#define TEST_LED3               GPIO_PD2
#define TEST_LED4               GPIO_PD3

#elif defined(MCU_CORE_TL751X)

#define  PM_TL7519X_128PIN      1
#define  PM_TL7519H_BGA94       2
#define  PM_PIN_MODE            PM_TL7519H_BGA94

#if(PM_PIN_MODE == PM_TL7519X_128PIN)
#define TEST_LED1               GPIO_PE3
#define TEST_LED2               GPIO_PA2
#define TEST_LED3               GPIO_PA1
#define TEST_LED4               GPIO_PA0
#else
#define TEST_LED1               GPIO_PB3
#define TEST_LED2               GPIO_PB4
#define TEST_LED3               GPIO_PI2
#define TEST_LED4               GPIO_PI1
#endif

#elif defined(MCU_CORE_TL753X)
#define PM_TL7539X_128PIN      1
#define PM_PIN_MODE            PM_TL7539X_128PIN
#define TEST_LED1              GPIO_PG5
#define TEST_LED2              GPIO_PG6
#define TEST_LED3              GPIO_PG7
#define TEST_LED4              GPIO_PG4

#elif defined(MCU_CORE_TL322X)

#define  PM_TL322X_96PIN        1
#define  PM_C1T371A111_96PIN    2
#define  PM_C1T378A20_80PIN     3
#define  PM_TL322X_56PIN        4
#define  PM_PIN_MODE            PM_TL322X_56PIN

#if(PM_PIN_MODE == PM_TL322X_96PIN)||(PM_PIN_MODE == PM_C1T378A20_80PIN)||(PM_PIN_MODE == PM_TL322X_56PIN)
#define TEST_LED1               GPIO_PC4
#define TEST_LED2               GPIO_PC5
#define TEST_LED3               GPIO_PC6
#define TEST_LED4               GPIO_PC7
#elif(PM_PIN_MODE == PM_C1T371A111_96PIN)
#define TEST_LED1               GPIO_PF3
#define TEST_LED2               GPIO_PF4
#define TEST_LED3               GPIO_PF5
#define TEST_LED4               GPIO_PF6
#endif

#endif

#if(TEST_MODE == FLASH_QUALITY_TEST)


#endif

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
