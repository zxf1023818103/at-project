/********************************************************************************************************
 * @file    usb_app_config.h
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

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PF0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PF1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PF2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PF3 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"

#if defined(MCU_CORE_B92) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X)
    /**
 * @brief   The POWER_SUPPLY_MODE defaults to VBUS_POWER_SUPPLY.
 */
    #define VBAT_POWER_SUPPLY 0
    #define VBUS_POWER_SUPPLY 1
    #define POWER_SUPPLY_MODE VBUS_POWER_SUPPLY
#endif

#if !defined(MCU_CORE_B91)
    #define AISO     0
    #define INT      1
    #define USB_MODE INT
#endif

#define USB_MOUSE       1
#define USB_KEYBOARD    2
#define USB_MICROPHONE  3 /* TL7518 this version is not support microphone data->SRAM, it will be added later. */
#define USB_SPEAKER     4 /* TL7518 this version is not support SRAM data->speaker, it will be added later. */
#define USB_CDC         5
#define USB_MIC_SPEAKER 6 /* TL7518 this version is not support microphone data->SRAM and SRAM data->speaker, it will be added later. */
#if defined(MCU_CORE_B91)
    #define USB_MASS_STORAGE 7
#endif
#define USB_PRINT     8 /* TL7518, TL721X, TL321X don't support hardware enumeration into print device.
                                 Please use software enumeration instead of hardware enumeration. */

#define USB_DEMO_TYPE USB_MOUSE

#if (USB_DEMO_TYPE == USB_MOUSE)
    #define USB_MOUSE_ENABLE 1
#elif (USB_DEMO_TYPE == USB_KEYBOARD)
    #define USB_KEYBOARD_ENABLE 1
#elif (USB_DEMO_TYPE == USB_MICROPHONE)
    #define USB_MIC_ENABLE 1
#elif (USB_DEMO_TYPE == USB_SPEAKER)
    #define USB_SPEAKER_ENABLE 1
#elif (USB_DEMO_TYPE == USB_CDC)
    #define USB_CDC_ENABLE 1
#elif (USB_DEMO_TYPE == USB_MIC_SPEAKER)
    #define USB_MIC_ENABLE     1
    #define USB_SPEAKER_ENABLE 1
#elif (USB_DEMO_TYPE == USB_PRINT)
    #define USB_PRINTER_ENABLE 1
#endif
#if defined(MCU_CORE_B91)
    #if ((USB_DEMO_TYPE == USB_MASS_STORAGE))
        #define USB_MASS_STORAGE_ENABLE 1

        #define SYS_NOR_FLASH_SUPPORT   1 //sys NOR_FLASH  1.Please refer to "sys_norflash.h" for the allocated address and capacity,2.Must be formatted for the first use
        #define SD_NAND_FLASH_SUPPORT   0 // External SD_NAND flash must be connected
        #if (SD_NAND_FLASH_SUPPORT) && (SYS_NOR_FLASH_SUPPORT)
            #define USB_DISK_NUM 1        //1 means 2 disk,0 means 1 disk, and so on
        #elif (SD_NAND_FLASH_SUPPORT) || (SYS_NOR_FLASH_SUPPORT)
            #define USB_DISK_NUM 0        //1 means 2 disk,0 means 1 disk, and so on
        #endif
    #endif
#endif

#define CHIP_VER_A0 0
#define CHIP_VER_A1 1
#define CHIP_VER    CHIP_VER_A1


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
