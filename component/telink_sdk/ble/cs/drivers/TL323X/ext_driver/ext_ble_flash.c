/********************************************************************************************************
 * @file    ble_flash.c
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    06,2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#include "ext_ble_flash.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#if (MCU_CORE_TYPE == CHIP_TYPE_TL323X)
/**
 * @brief   BLE parameters on flash
 */
typedef struct __attribute__((packed))
{
    u8 cap_frqoffset_en;
    u8 cap_frqoffset_value;

} nv_para_t;

/**
 * @brief   flash voltage definition
 */
typedef enum
{
    USER_CALIB_FROM_FLASH = 0x01,
    USER_CALIB_FROM_OTP   = 0x02,
} user_calib_from_e;

_attribute_data_retention_ nv_para_t blc_nvParam;


/* global variable for some flash sector address, initial value use 2M flash.
 * attention that they will be changed in function "blc readFlashSize_autoConfigCustomFlashSector"  */
_attribute_data_retention_ u32 flash_sector_mac_address    = CFG_ADR_MAC_2M_FLASH;
_attribute_data_retention_ u32 flash_sector_calibration    = CFG_ADR_CALIBRATION_2M_FLASH;
_attribute_data_retention_ u32 flash_sector_smp_storage    = FLASH_ADR_SMP_PAIRING_2M_FLASH;
_attribute_data_retention_ u32 flash_sector_simple_sdp_att = FLASH_SDP_ATT_ADDRESS_2M_FLASH;

_attribute_data_retention_ unsigned int  blc_flash_mid    = 0;
_attribute_data_retention_ unsigned int  blc_flash_vendor = 0;
_attribute_data_retention_ unsigned char blc_flash_capacity;
extern void write_storage_from_ram(unsigned int addr, unsigned int len, unsigned int *data);
extern void read_storage_to_ram(unsigned int addr, unsigned int len, unsigned char *data);
/**
 * @brief       This function is used to read flash mid, get flash vendor, and set flash capacity
 * @param[in]   none
 * @return      none
 */
void blc_flash_read_mid_get_vendor_set_capacity(void)
{
    /*
        Flash Type  uid CMD     MID         Company
        P25Q80U     0x4b        0x146085    PUYA
        P25Q16SU    0x4b        0x156085    PUYA
        P25Q32SU    0x4b        0x166085    PUYA
        P25Q128L    0x4b        0x186085    PUYA
        PY25Q128H   0x4b        0x182085    PUYA
        GD25LQ16E   0x4b        0x1560c8    GD
        P25Q80SU    0x4b        0x146085    PUYA
        GD25LE80E   0x4b        0x1460c8    GD
        GD25LE16E   0x4b        0x1560c8    GD
     */
    /* attention: blc_flash_mid/blc_flash_vendor/blc_flash_capacity will be used by application and stack later
     * so do not change code here */
    blc_flash_mid      = ble_flash_read_mid();
    blc_flash_vendor   = flash_get_vendor(blc_flash_mid);
    blc_flash_capacity = ((blc_flash_mid & 0x00ff0000) >> 16);

    tlkapi_send_string_u32s(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] flash initialization", blc_flash_mid, blc_flash_vendor, blc_flash_capacity);
}

/**
 * @brief       This function can automatically recognize the flash size,
 *              and the system selects different customized sector according
 *              to different sizes.
 * @param[in]   none
 * @return      none
 */
void blc_readFlashSize_autoConfigCustomFlashSector(void)
{
    blc_flash_read_mid_get_vendor_set_capacity();

    if (0) {
    }
#if (FLASH_P25Q80U_SUPPORT_EN || FLASH_P25Q80SU_SUPPORT_EN || FLASH_GD25LE80E_SUPPORT_EN || FLASH_GD25LQ80E_SUPPORT_EN) //1M
    else if (blc_flash_capacity == FLASH_SIZE_1M) {
        flash_sector_mac_address = CFG_ADR_MAC_1M_FLASH;
        flash_sector_calibration = CFG_ADR_CALIBRATION_1M_FLASH;
        flash_sector_smp_storage = FLASH_ADR_SMP_PAIRING_1M_FLASH;

    #if (ACL_CENTRAL_SIMPLE_SDP_ENABLE)
        flash_sector_simple_sdp_att = FLASH_SDP_ATT_ADDRESS_1M_FLASH;
    #endif

        tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] 1M Flash, MAC on %x\r\n", flash_sector_mac_address);
    }
#endif
#if (FLASH_P25Q16SU_SUPPORT_EN || FLASH_GD25LQ16E_SUPPORT_EN || FLASH_GD25LE16E_SUPPORT_EN) //2M
    else if (blc_flash_capacity == FLASH_SIZE_2M) {
        flash_sector_mac_address = CFG_ADR_MAC_2M_FLASH;
        flash_sector_calibration = CFG_ADR_CALIBRATION_2M_FLASH;
        flash_sector_smp_storage = FLASH_ADR_SMP_PAIRING_2M_FLASH;

    #if (ACL_CENTRAL_SIMPLE_SDP_ENABLE)
        flash_sector_simple_sdp_att = FLASH_SDP_ATT_ADDRESS_2M_FLASH;
    #endif

        tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] 2M Flash, MAC on %x\r\n", flash_sector_mac_address);
    }
#endif
#if (FLASH_P25Q32SU_SUPPORT_EN) //4M
    else if (blc_flash_capacity == FLASH_SIZE_4M) {
        flash_sector_mac_address = CFG_ADR_MAC_4M_FLASH;
        flash_sector_calibration = CFG_ADR_CALIBRATION_4M_FLASH;
        flash_sector_smp_storage = FLASH_ADR_SMP_PAIRING_4M_FLASH;

    #if (ACL_CENTRAL_SIMPLE_SDP_ENABLE)
        flash_sector_simple_sdp_att = FLASH_SDP_ATT_ADDRESS_4M_FLASH;
    #endif

        tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] 4M Flash, MAC on %x\r\n", flash_sector_mac_address);
    }
#endif
#if (FLASH_P25Q128L_SUPPORT_EN || FLASH_P25Q128H_SUPPORT_EN) //16M
    else if (blc_flash_capacity == FLASH_SIZE_16M) {
        flash_sector_mac_address = CFG_ADR_MAC_16M_FLASH;
        flash_sector_calibration = CFG_ADR_CALIBRATION_16M_FLASH;
        flash_sector_smp_storage = FLASH_ADR_SMP_PAIRING_16M_FLASH;

    #if (ACL_CENTRAL_SIMPLE_SDP_ENABLE)
        flash_sector_simple_sdp_att = FLASH_SDP_ATT_ADDRESS_16M_FLASH;
    #endif

        tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] 16M Flash, MAC on  %x\r\n", flash_sector_mac_address);
    }
#endif
    else {
        /*This SDK do not support other flash size except what listed above
          If code stop here, please check your Flash */
        tlkapi_send_string_data(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] flash size do not support !!!", &blc_flash_capacity, 1);
        while (1) {
#if (TLKAPI_DEBUG_ENABLE)
            tlkapi_debug_handler();
#endif
        }
    }
}

/**
 * @brief      This function serves to update rf frequency offset.
 * @param[in]  velfrom - the calibration value from flash or otp.
 * @param[in]  addr - the frequency offset value address of flash or otp.
 * @return     1 - the frequency offset update, 0 - the frequency offset is not update.
 */
unsigned char user_calib_freq_offset(user_calib_from_e velfrom, unsigned int addr)
{
    unsigned char frequency_offset_value = 0xff;
    if (velfrom == USER_CALIB_FROM_FLASH) {
#if MCU_RUN_IN_NVM
        read_storage_to_ram(addr, 1, &frequency_offset_value);
#else
        flash_read_page(addr, 1, &frequency_offset_value);
#endif
    }
    if (0xff != frequency_offset_value) {
        /*<! BLE USED */
        blc_nvParam.cap_frqoffset_en    = 1;
        blc_nvParam.cap_frqoffset_value = frequency_offset_value;
        /*<! BLE USED END */
        rf_update_internal_cap(frequency_offset_value);
        tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] user_calib_freq_offset value=0x%02X", frequency_offset_value);
        return 1;
    }
    tlkapi_printf(APP_FLASH_INIT_LOG_EN, "[FLASH][INI] user_calib_freq_offset value error\r\n");
    return 0;
}

/**
 * @brief       load customized parameters (from Flash/EFUSE) when MCU power on or wake_up from deepSleep mode
 * @param[in]   none
 * @return      none
 */
void blc_app_loadCustomizedParameters_normal(void)
{
// Check if flash sector for calibration is valid
#if  (MCU_CORE_TYPE == MCU_CORE_TL721X)
   otp_calib_adc_vref();
#endif

#if  (MCU_CORE_TYPE == MCU_CORE_TL321X)
    efuse_calib_adc_vref();
#endif
#if (MCU_CORE_TYPE == MCU_CORE_TL322X)
    /******get sar adc calibration value from EFUSE********/
    extern drv_api_status_e efuse_calib_sar_adc_vref(void);
    efuse_calib_sar_adc_vref();
    /******get sd_adc calibration value from EFUSE********/
    extern drv_api_status_e efuse_calib_sd_adc_vref(void);
    efuse_calib_sd_adc_vref();
#endif

#if (MCU_CORE_TYPE == MCU_CORE_TL323X)
    /******get sd_adc calibration value from EFUSE********/
    extern drv_api_status_e efuse_calib_sd_adc_vref(void);
    efuse_calib_sd_adc_vref();
#endif

    // Check if flash sector for calibration is valid
    if (flash_sector_calibration) {
#if ((MCU_CORE_TYPE == MCU_CORE_B91) || (MCU_CORE_TYPE == MCU_CORE_B92) || \
    (MCU_CORE_TYPE == MCU_CORE_TL721X) || (MCU_CORE_TYPE == MCU_CORE_TL321X) || MCU_CORE_TYPE == MCU_CORE_TL322X) || \
    (MCU_CORE_TYPE == MCU_CORE_TL323X)
        // Load RF frequency offset calibration
        user_calib_freq_offset(USER_CALIB_FROM_FLASH, (flash_sector_calibration + CALIB_OFFSET_CAP_INFO));
#endif
    }
}

/**
 * @brief       load customized parameters (from SRAM) MCU wake_up from deepSleep_retention mode
 * @param[in]   none
 * @return      none
 */
_attribute_ram_code_ void blc_app_loadCustomizedParameters_deepRetn(void)
{
    if (blc_nvParam.cap_frqoffset_en) {
        rf_update_internal_cap(blc_nvParam.cap_frqoffset_value);
    }
}

/*
 *B91:  VVWWXX 775FD8 YYZZ
 *B92:  VVWWXX B4CF3C YYZZ

 * public_mac:
 *              B91 : VVWWXX 775FD8
 *              B92 : VVWWXX B4CF3C
 *
 * random_static_mac: VVWWXXYYZZ C0
 */
/**
 * @brief       This function is used to initialize the MAC address
 * @param[in]   flash_addr - flash address for MAC address
 * @param[in]   mac_public - public address
 * @param[in]   mac_random_static - random static MAC address
 * @return      none
 */
_attribute_no_inline_ void blc_initMacAddress(int flash_addr, u8 *mac_public, u8 *mac_random_static)
{
    if (flash_sector_mac_address == 0) {
        return;
    }

    int rand_mac_byte3_4_read_OK = 0;

    u8  mac_read[8];

#if MCU_RUN_IN_NVM
    read_storage_to_ram(flash_addr, 8, mac_read);
#else
    flash_read_page(flash_addr, 8, mac_read);
#endif

    u8 value_rand[5];
    generateRandomNum(5, value_rand);

#if MCU_RUN_IN_NVM
    u8 ff_six_byte[6] = {0};
#else
    u8 ff_six_byte[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#endif
    if (memcmp(mac_read, ff_six_byte, 6)) { //read MAC address on flash success
        memcpy(mac_public, mac_read, 6);    //copy public address from flash

        if (mac_read[6] != 0xFF && mac_read[7] != 0xFF) {
            mac_random_static[3]     = mac_read[6];
            mac_random_static[4]     = mac_read[7];
            rand_mac_byte3_4_read_OK = 1;
        }
    } else {                                    //no MAC address on flash

#if (BUILT_IN_MAC_ON_DEVICE)
        if (get_device_mac_address(mac_read, 8)) //read device MAC address
        {
            memcpy(mac_public, mac_read, 6);    //copy public address from device

            mac_random_static[3]     = mac_read[6];
            mac_random_static[4]     = mac_read[7];
            rand_mac_byte3_4_read_OK = 1;
        } else
#endif
        {
            mac_public[0] = value_rand[0];
            mac_public[1] = value_rand[1];
            mac_public[2] = value_rand[2];

            /* company id */
            mac_public[3] = U32_BYTE0(PDA_COMPANY_ID);
            mac_public[4] = U32_BYTE1(PDA_COMPANY_ID);
            mac_public[5] = U32_BYTE2(PDA_COMPANY_ID);

#if MCU_RUN_IN_FLASH
            if(flash_prot_op_cb){
                flash_prot_op_cb(FLASH_OP_EVT_APP_WRITE_MAC_ADDR_BEGIN, flash_addr, flash_addr + 6);
            }
#endif

#if MCU_RUN_IN_NVM
            write_storage_from_ram(flash_addr, 6, (unsigned int*)mac_public); //store public address on flash for future use
#else
            flash_write_page(flash_addr, 6, mac_public); //store public address on flash for future use
#endif

#if MCU_RUN_IN_FLASH
            if(flash_prot_op_cb){
                flash_prot_op_cb(FLASH_OP_EVT_APP_WRITE_MAC_ADDR_END, flash_addr, flash_addr + 6);
            }
#endif
        }
    }


    mac_random_static[0] = mac_public[0];
    mac_random_static[1] = mac_public[1];
    mac_random_static[2] = mac_public[2];
    mac_random_static[5] = 0xC0; //for random static

    if (!rand_mac_byte3_4_read_OK) {
        mac_random_static[3] = value_rand[3];
        mac_random_static[4] = value_rand[4];

#if MCU_RUN_IN_FLASH
        if(flash_prot_op_cb){
            flash_prot_op_cb(FLASH_OP_EVT_APP_WRITE_MAC_ADDR_BEGIN, flash_addr + 6, flash_addr + 8);
        }
#endif

#if MCU_RUN_IN_NVM
        write_storage_from_ram(flash_addr + 6, 2, (unsigned int*)(mac_random_static + 3)); //store random address on flash for future use
#else
        flash_write_page(flash_addr + 6, 2, (u8 *)(mac_random_static + 3)); //store random address on flash for future use
#endif
        
#if MCU_RUN_IN_FLASH
        if(flash_prot_op_cb){
            flash_prot_op_cb(FLASH_OP_EVT_APP_WRITE_MAC_ADDR_END, flash_addr + 6, flash_addr + 8);
        }
#endif
    }
}
#endif
