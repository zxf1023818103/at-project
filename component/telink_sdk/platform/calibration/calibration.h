/********************************************************************************************************
 * @file    calibration.h
 *
 * @brief   This is the header file for tl323x
 *
 * @author  Driver Group
 * @date    2024
 *
 * @par     Copyright (c) 2024, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef VENDOR_COMMON_CALIBRATION_H_
#define VENDOR_COMMON_CALIBRATION_H_

#include "driver.h"

/**
 * Note: If a new storage capacity FLASH is added, in addition to updating this file,
 * the drivers/lib/include/sys.h file also needs to be updated simultaneously,
 * because the location of core voltage calibration is recorded in that file.
 */
#ifndef FLASH_CAP_VALUE_ADDR_64K
    #define FLASH_CAP_VALUE_ADDR_64K 0xe000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_128K
    #define FLASH_CAP_VALUE_ADDR_128K 0x1e000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_512K
    #define FLASH_CAP_VALUE_ADDR_512K 0x7e000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_1M
    #define FLASH_CAP_VALUE_ADDR_1M 0xfe000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_2M
    #define FLASH_CAP_VALUE_ADDR_2M 0x1fe000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_4M
    #define FLASH_CAP_VALUE_ADDR_4M 0x3fe000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_8M
    #define FLASH_CAP_VALUE_ADDR_8M 0x7fe000
#endif

#ifndef FLASH_CAP_VALUE_ADDR_16M
    #define FLASH_CAP_VALUE_ADDR_16M 0xffe000
#endif

/********************IEEE ADDRESS IN FLASH***********************/
#ifndef FLASH_IEEE_ADDR_LOCATION_64K
    #define FLASH_IEEE_ADDR_LOCATION_64K 0xf000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_128K
    #define FLASH_IEEE_ADDR_LOCATION_128K 0x1f000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_512K
    #define FLASH_IEEE_ADDR_LOCATION_512K 0x7f000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_1M
    #define FLASH_IEEE_ADDR_LOCATION_1M 0xff000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_2M
    #define FLASH_IEEE_ADDR_LOCATION_2M 0x1ff000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_4M
    #define FLASH_IEEE_ADDR_LOCATION_4M 0x3ff000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_8M
    #define FLASH_IEEE_ADDR_LOCATION_8M 0x7ff000
#endif

#ifndef FLASH_IEEE_ADDR_LOCATION_16M
    #define FLASH_IEEE_ADDR_LOCATION_16M 0xfff000
#endif

typedef enum
{
    IEEE_ADDR_NOT_EXIST,
    IEEE_ADDR_FROM_FLASH,
    IEEE_ADDR_FROM_EFUSE,
} ieee_addr_source_e;

/**
 * @brief       This function is used to calibrate the user's parameters.
 *              This function is to read the calibration value stored in efuse and flash,
 *              and use the calibration value to configure the chip to improve chip performance.
 *              (reduce adc measurement error, reduce frequency offset, etc.)
 * @return      none.
 */
void calibration_func(void);

/**
 * @brief      This function serves to update rf frequency offset.
 * @param[in]  addr - the frequency offset value address of flash.
 * @return     1 - the frequency offset update, 0 - the frequency offset is not update.
 */
unsigned char user_calib_freq_offset(unsigned int addr);

/**
 * @brief      This function serves to read IEEE address from FLASH or EFUSE.
 * @param[in]  addr - the IEEE address of flash.
 * @param[out] buf  - Pointer to IEEE address buffer
 * @return     ieee_addr_source_e
 */
ieee_addr_source_e user_get_ieee_addr(unsigned int addr, unsigned char *mac_addr_buf);
#endif /* VENDOR_COMMON_CALIBRATION_H_ */
