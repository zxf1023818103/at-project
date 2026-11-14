/********************************************************************************************************
 * @file    app_config.h
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

#if defined(__cplusplus)
extern "C"
{
#endif

#include "build_config.h"
#if !defined(MCU_CORE_TL752X)
#include "led_and_key_config.h"
#endif

#if defined(CURRENT_BUILD_UART_Demo)
#if defined(MCU_CORE_TL523X)
#include "app_config/uart_app_config_1v1.h"
#else
#include "app_config/uart_app_config_1v0.h"
#endif
#endif

#if defined(CURRENT_BUILD_ADC_Demo)
#if defined(MCU_CORE_B91) || defined(MCU_CORE_B92)
#include "app_config/adc_app_config_1v0.h"
#endif
#if defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X)
#include "app_config/adc_app_config_1v1.h"
#elif defined(MCU_CORE_TL322X)
#include "app_config/adc_app_config_1v2.h"
#endif
#endif

#if defined(CURRENT_BUILD_AES_Demo)
#include "app_config/aes_app_config.h"
#endif

#if defined(CURRENT_BUILD_ALG_REG_Demo)
#include "app_config/alg_reg_app_config.h"
#endif

#if defined(CURRENT_BUILD_Charger_Demo)
#if defined(MCU_CORE_B91)
#include "app_config/charger_app_config_b91.h"
#endif
#if defined(MCU_CORE_B92)
#include "app_config/charger_app_config_b92.h"
#endif
#endif

#if defined(CURRENT_BUILD_CTB_Demo)
#if defined(MCU_CORE_B92)
#include "app_config/ctb_app_config_1V0.h"
#endif
#if defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL322X)
#include "app_config/ctb_app_config_1V1.h"
#endif
#endif

#if defined(CURRENT_BUILD_Flash_Demo) || defined(CURRENT_BUILD_N22_Flash_Demo_BY_MCUA)
#include "app_config/flash_app_config.h"
#endif

#if defined(CURRENT_BUILD_ALG_Keyscan_Demo)
#include "app_config/keyscan_ana_app_config.h"
#endif

#if defined(CURRENT_BUILD_AUDIO_Demo)
#if defined(MCU_CORE_B91)
#include "app_config/audio_app_config_1v0.h"
#endif
#if defined(MCU_CORE_B92)
#include "app_config/audio_app_config_1v1.h"
#endif
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL521X)
#include "app_config/audio_app_config_1v3.h"
#endif
#if (defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL7518))
#include "app_config/audio_app_config_1v2.h"
#endif
#if defined(MCU_CORE_TL752X)
#include "app_config/audio_app_config_1v4.h"
#endif
#if defined(MCU_CORE_TL753X)
#include "app_config/audio_app_config_1v5.h"
#endif
#endif

#if defined(CURRENT_BUILD_CHACHA20_POLY1305_Demo)
#if defined(MCU_CORE_TL7518)||defined(MCU_CORE_TL721X)
#include "app_config/chacha20_play1305_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_COREMARK) || defined(CURRENT_BUILD_D25F_COREMARK) || defined(CURRENT_BUILD_N22_COREMARK)
#include "app_config/coremark_app_config.h"
#endif

#if defined(CURRENT_BUILD_Debug_Demo) || defined(CURRENT_BUILD_Debug_RAM_Demo)
#include "app_config/debug_app_config.h"
#endif

#if defined(CURRENT_BUILD_DHRYSTONE) || defined(CURRENT_BUILD_D25F_DHRYSTONE) || defined(CURRENT_BUILD_N22_DHRYSTONE)
#include "app_config/dhrystone_app_config.h"
#endif

#if defined(CURRENT_BUILD_Display_Demo)
#include "app_config/display_app_config.h"
#endif

#if defined(CURRENT_BUILD_GPIO_Demo)
#include "app_config/gpio_app_config.h"
#endif

#if defined(CURRENT_BUILD_HASH_Demo)
#include "app_config/hash_app_config.h"
#endif

#if defined(CURRENT_BUILD_I2C_Demo)
#include "app_config/i2c_app_config.h"
#endif

#if defined(CURRENT_BUILD_Keyscan_Demo)
#include "app_config/keyscan_app_config.h"
#endif

#if defined(CURRENT_BUILD_Keyscan_Ana_Demo)
#include "app_config/keyscan_ana_app_config.h"
#endif

#if defined(CURRENT_BUILD_LIN_Demo)
#include "app_config/lin_app_config.h"
#endif

#if defined(CURRENT_BUILD_LPC_Demo)
#include "app_config/lpc_app_config.h"
#endif

#if defined(CURRENT_BUILD_MDEC_Demo)
#include "app_config/mdec_app_config.h"
#endif

#if defined(CURRENT_BUILD_MULTI_CORE_Demo)
#include "app_config/multi_core_app_config.h"
#endif

#if defined(CURRENT_BUILD_QDEC_Demo)
#include "app_config/qdec_app_config.h"
#endif

#if defined(CURRENT_BUILD_PKE_Demo)
#if defined(MCU_CORE_B91) || defined(MCU_CORE_B92)
#include "app_config/pke_app_config_1v0.h"
#else
#include "app_config/pke_app_config_1v1.h"
#endif
#endif

#if defined(CURRENT_BUILD_RPA_Demo)
#include "app_config/rpa_app_config.h"
#endif

#if defined(CURRENT_BUILD_s7816_Demo)
#include "app_config/s7816_app_config.h"
#endif

#if defined(CURRENT_BUILD_SKE_Demo)
#include "app_config/ske_app_config.h"
#endif

#if defined(CURRENT_BUILD_SPI_Demo)
#if defined(MCU_CORE_B91)
#include "app_config/spi_app_config_1v0.h"
#endif
#if defined(MCU_CORE_B92) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL522X)
#include "app_config/spi_app_config_1v1.h"
#elif defined(MCU_CORE_TL752X)
#include "app_config/spi_app_config_1v2.h"
#endif
#endif

#if defined(CURRENT_BUILD_TRNG_Demo)
#include "app_config/trng_app_config.h"
#endif


#if defined(CURRENT_BUILD_Security_MultiBin_SDK_Demo) || defined(CURRENT_BUILD_Security_MultiBin_USER_Demo)
#include "app_config/security_multibin_app_config.h"
#endif

#if defined(CURRENT_BUILD_BQB_Demo)
#include "app_config/bqb_app_config.h"
#endif

#if defined(CURRENT_BUILD_EMI_BQB_Demo)
#include "app_config/emi_bqb_app_config.h"
#endif

#if defined(CURRENT_BUILD_TRAP_Demo)
#include "app_config/trap_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_TRAP_Demo)
#include "app_config/n22_trap_app_config.h"
#endif

#if defined(CURRENT_BUILD_USB_Demo)
#include "app_config/usb_app_config.h"
#endif

#if defined(CURRENT_BUILD_USB0_Demo)
#include "app_config/usb0_app_config.h"
#endif

#if defined(CURRENT_BUILD_VCD_Demo)
#include "app_config/vcd_app_config.h"
#endif


#if defined(CURRENT_BUILD_STIMER_Demo)
#include "app_config/stimer_app_config.h"
#endif

#if defined(CURRENT_BUILD_TIMER_Demo)
#include "app_config/timer_app_config.h"
#endif

#if defined(CURRENT_BUILD_PWM_Demo)
#include "app_config/pwm_app_config.h"
#endif

#if defined(CURRENT_BUILD_IR_LEARN_Demo)
#include "app_config/ir_learn_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_STIMER_Demo) || defined(CURRENT_BUILD_N22_STimer_Demo)
#include "app_config/n22_stimer_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_Timer_BB_Demo)
#include "app_config/n22_timer_bb_app_config.h"
#endif

#if defined(CURRENT_BUILD_RF_Demo)||defined(CURRENT_BUILD_D25F_RF_Demo)||defined(CURRENT_BUILD_N22_RF_Demo)
#include "app_config/rf_app_config.h"
#endif

#if defined(CURRENT_BUILD_OTP_Demo)
#include "app_config/otp_app_config.h"
#endif

#if defined(CURRENT_BUILD_NPE_Demo)
#include "app_config/npe_app_config.h"
#endif

#if defined(CURRENT_BUILD_HADM_Demo)
#include "app_config/hadm_app_config.h"
#endif

#if defined(CURRENT_BUILD_BQB_HCI_Demo)
#include "app_config/bqb_hci_app_config.h"
#endif

#if defined(CURRENT_BUILD_DUT_Demo)
#include "app_config/dut_app_config.h"
#endif

#if defined(CURRENT_BUILD_DUT_Demo)
#include "app_config/dut_app_config.h"
#endif

#if defined(CURRENT_BUILD_DUT_Test)
#include "app_config/dut_test_app_config.h"
#endif

#if defined(CURRENT_BUILD_Swire_Demo)
#include "app_config/swire_app_config.h"
#endif

#if defined(CURRENT_BUILD_Mbedtls_Demo)
#include "app_config/mbedtls_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_Test_Demo_Booloader_By_DMA)
#include "app_config/n22_test_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_Test_Demo_Booloader_By_N22_MCU)
#include "app_config/n22_test_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_Test_Demo)
#include "app_config/n22_test_app_config.h"
#endif
#if defined(CURRENT_BUILD_N22_Test_Demo_BY_MCUA)
#include "app_config/test_app_config.h"
#endif
#if defined(CURRENT_BUILD_N22_Test_Demo_Bootloader_by_N22_NVM_MCU)
#include "app_config/n22_test_app_config.h"
#endif

#if defined(CURRENT_BUILD_PEM_Demo)
#include "app_config/pem_app_config.h"
#endif

#if defined(CURRENT_BUILD_Test_Demo)
#include "app_config/test_app_config.h"
#if defined(MCU_CORE_TL323X)
#include "app_config/sd_adc_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_N22_Test_Demo)
#if defined(MCU_CORE_TL322X)
#include "app_config/test_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_Test_N22_Demo)
#include "app_config/test_app_config.h"
#endif

#if defined(CURRENT_BUILD_Test_NVM_Demo)
#include "app_config/test_app_config.h"
#endif

#if defined(CURRENT_BUILD_Test_NVM_N22_Demo)
#include "app_config/test_app_config.h"
#endif

#if defined(CURRENT_BUILD_NVM_Demo)
#include "app_config/nvm_app_config.h"
#endif

#if defined(CURRENT_BUILD_N22_NVM_Demo)
#include "app_config/n22_nvm_app_config.h"
#endif

#if defined(CURRENT_BUILD_PM_Demo)
#include "app_config/pm_app_config.h"
#endif

#if defined(CURRENT_BUILD_PM_D25F)
#include "app_config/pm_app_config.h"
#endif

#if defined(CURRENT_BUILD_PM_Test_Demo)
#include "app_config/pm_test_config.h"
#endif

#if defined(CURRENT_BUILD_EMI_Demo)
#include "app_config/emi_app_config.h"
#endif

#if defined(CURRENT_BUILD_Auto_Test_Demo)
#include "app_config/auto_test_app_config.h"
#endif

#if defined(CURRENT_BUILD_Freertos_Demo)
#include "app_config/freertos_app_config.h"
#endif

#if defined(CURRENT_BUILD_AXON_Demo)
#include "app_config/axon_app_config.h"
#endif

#if defined(CURRENT_BUILD_Camera_Demo)
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL322X)
#include "app_config/dk1_camera_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_Codec_Demo)
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X)
#include "app_config/dk1_codec_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_Sensor_Lcd_Demo)
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X)
#include "app_config/dk1_sensor_lcd_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_SpiFlash_Demo)
#if defined(MCU_CORE_TL322X)
#include "app_config/dk1_spi_flash_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_SD_ADC_Demo)
#if defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)
#include "app_config/sd_adc_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_CAN_Demo)
#include "app_config/can_app_config.h"
#endif

#if defined(CURRENT_BUILD_I3C_Demo)
#include "app_config/i3c_app_config.h"
#endif
#if defined(CURRENT_BUILD_RZ_Demo)
#include "app_config/rz_app_config.h"
#endif

#if defined(CURRENT_BUILD_SD_HOST_Demo)
#include "app_config/sd_host_config.h"
#endif

#if defined(CURRENT_BUILD_32K_RC_Demo)
#include "app_config/32krc_app_config.h"
#endif


#if defined(CURRENT_BUILD_TAHDT_Demo)||defined(CURRENT_BUILD_D25F_TAHDT_Demo)||defined(CURRENT_BUILD_N22_TAHDT_Demo)
#if defined(MCU_CORE_TL752X)
#include "app_config/tahdt_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_DMA_Demo)
#if defined(MCU_CORE_TL752X)
#include "app_config/dma_app_config.h"
#endif
#endif

#if defined(CURRENT_BUILD_PMP_Demo)
#include "app_config/pmp_app_config.h"
#endif

#if defined(CURRENT_BUILD_Programmer_Demo)
#include "app_config/programmer_app_config.h"
#endif

#if defined(__cplusplus)
}
#endif

