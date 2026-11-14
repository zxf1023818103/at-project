/********************************************************************************************************
 * @file    calibration.c
 *
 * @brief   This is the source file for tl323x
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
#include "calibration.h"
#include "lib/include/efuse.h"
/**
 * @brief      This function serves to update rf frequency offset.
 * @param[in]  addr - the frequency offset value address of flash.
 * @return     1 - the frequency offset update, 0 - the frequency offset is not update.
 */
unsigned char user_calib_freq_offset(unsigned int addr)
{
    unsigned char frequency_offset_value = 0xff;

    flash_read_page(addr, 1, &frequency_offset_value);

    if (0xff != frequency_offset_value) {
        rf_update_internal_cap(frequency_offset_value);
        return 1;
    }

    return 0;
}

/**
 * @brief       This function is used to calibrate the user's parameters.
 *              This function is to read the calibration value stored in efuse and flash,
 *              and use the calibration value to configure the chip to improve chip performance.
 *              (reduce adc measurement error, reduce frequency offset, etc.)
 * @return      none.
 */
void calibration_func(void)
{
    unsigned char flash_mid[4];
    unsigned char flash_uid[16];
    unsigned char flash_mid_sure = 0;
    unsigned int  ieee_flash_pos = 0, cap_value_addr = 0;
    unsigned char ieee_addr[8];

    /******get sd_adc calibration value from EFUSE********/
   extern drv_api_status_e efuse_calib_sd_adc_vref(void);
   efuse_calib_sd_adc_vref();

    /******check for flash mid********/
    flash_mid_sure = flash_read_mid_uid_with_check((unsigned int *)flash_mid, flash_uid);


    if (1 == flash_mid_sure) {
        switch (flash_mid[2]) {
        case FLASH_SIZE_64K:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_64K;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_64K;
            break;
        case FLASH_SIZE_128K:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_128K;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_128K;
            break;
        case FLASH_SIZE_512K:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_512K;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_512K;
            break;
        case FLASH_SIZE_1M:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_1M;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_1M;
            break;
        case FLASH_SIZE_2M:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_2M;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_2M;
            break;
        case FLASH_SIZE_4M:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_4M;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_4M;
            break;
        case FLASH_SIZE_8M:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_8M;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_8M;
            break;
        case FLASH_SIZE_16M:
            cap_value_addr = FLASH_CAP_VALUE_ADDR_16M;
            ieee_flash_pos = FLASH_IEEE_ADDR_LOCATION_16M;
            break;
        default:
            break;
        }
        if (cap_value_addr) {
            user_calib_freq_offset(cap_value_addr);
        }
        if (ieee_flash_pos) {
            user_get_ieee_addr(ieee_flash_pos, ieee_addr);
        }

    } else {
        user_get_ieee_addr(0, ieee_addr);
    }

    pm_efuse_calib_vdd1v8_voltage();
}

/**
 * @brief      This function serves to check if IEEE address is legal.
 * @param[in]  value  - Pointer to value address buffer
 * @return     0: value address is illegal, 1:value address is legal
 */
unsigned char user_check_ieee_addr(unsigned char *value)
{
    //IEEE address is legal if it is not all 0x00 or all 0xFF.
    for (int i = 1; i < 8; i++) {
        if (value[i] != value[0]) {
            return 1;
        }
    }
    //If all elements are the same, check if it is all 0x00 or all 0xFF
    return (value[0] == 0x00 || value[0] == 0xFF) ? 0 : 1;
}

/**
 * @brief      This function serves to read IEEE address from EFUSE.
 * @param[out] buf  - Pointer to IEEE address buffer
 * @return     ieee_addr_source_e
 */
ieee_addr_source_e user_get_efuse_ieee_addr(unsigned char *buf)
{
    //Read IEEE address priority: FLASH > EFUSE
    efuse_get_ieee_addr(buf);
    if (user_check_ieee_addr(buf)) {
        return IEEE_ADDR_FROM_EFUSE;
    } else {
        return IEEE_ADDR_NOT_EXIST;
    }
}


/**
 * @brief      This function serves to read IEEE address from FLASH or EFUSE.
 * @param[in]  addr - the IEEE address of flash
 *                    0:     Read the IEEE address from EFUSE.
 *                    other: first read the IEEE address from the flash memory.
 *                           If it is not a valid value, then read it from EFUSE.
 * @param[out] buf  - Pointer to IEEE address buffer(IEEE address is 8bytes)
 * @return     ieee_addr_source_e
 */
ieee_addr_source_e user_get_ieee_addr(unsigned int addr, unsigned char *buf)
{
    if (addr == 0) {
        return user_get_efuse_ieee_addr(buf);
    } else {
        //Read IEEE address priority: FLASH > EFUSE
        flash_read_page(addr, 8, buf);
        if (user_check_ieee_addr(buf)) {
            return IEEE_ADDR_FROM_FLASH;
        } else {
            return user_get_efuse_ieee_addr(buf);
        }
    }
}
