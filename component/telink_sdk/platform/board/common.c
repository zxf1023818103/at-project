/********************************************************************************************************
 * @file    common.c
 *
 * @brief   This is the source file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2023
 *
 * @par     Copyright (c) 2023, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#include "common.h"
#include "driver.h"
#define JTAG_MODE 0

volatile unsigned int g_debug_flag;

#if defined(MCU_CORE_TL651X)
void platform_init(void)
{
    WRITE_REG(*(unsigned int *)0xa0120008,0x04000000);
}

#else

/*
 * @note The flash protection size has been allocated by default, refer to the comment FLASH_PROTECT_MODIFY_CONFIG for details.
 */
flash_user_defined_list_t flash_init_list[] = {
#if defined(MCU_CORE_B91)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x14345e, FLASH_LOCK_LOW_512K_MID14345E},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085  },
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085  },
    //16M
    {0x182085, FLASH_LOCK_LOW_8M_MID182085  }

#elif defined(MCU_CORE_B92)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    {0x1660c8, FLASH_LOCK_LOW_2M_MID1660C8},
#elif defined(MCU_CORE_TL7518)
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085}
#elif defined(MCU_CORE_TL751X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    //8M
    {0x176085, FLASH_LOCK_LOW_4M_MID176085},
#elif defined(MCU_CORE_TL752X)
    //4M
    {0x1640c8, FLASH_LOCK_LOW_2M_MID1640C8},
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    {0x16405e, FLASH_LOCK_LOW_2M_MID16405E},
    //2M
    {0x1571cd, FLASH_LOCK_LOW_1M_MID1571CD},
#elif defined(MCU_CORE_TL753X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    {0x1660cd, FLASH_LOCK_LOW_2M_MID1660CD},
    //8M
    {0x176085, FLASH_LOCK_LOW_4M_MID176085},
    {0x174120, FLASH_LOCK_LOW_4M_MID174120},
#elif defined(MCU_CORE_TL721X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
#elif defined(MCU_CORE_TL321X)
    //128K
    {0x1151CD, FLASH_LOCK_LOW_64K_MID1151CD},
    //256K
    {0x124585, FLASH_LOCK_LOW_128K_MID124585},
    {0x1271CD, FLASH_LOCK_LOW_128K_MID1271CD},
    //512K
    {0x136085, FLASH_LOCK_LOW_256K_MID136085},
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
#elif defined(MCU_CORE_TL322X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
#elif defined(MCU_CORE_TL522X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    //8M
    {0x176085, FLASH_LOCK_LOW_4M_MID176085},
#elif defined(MCU_CORE_TL323X)
    //1M
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    {0x1471cd, FLASH_LOCK_LOW_512K_MID1471CD},
    //2M
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
    {0x1571cd, FLASH_LOCK_LOW_1M_MID1571CD},
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085},
    {0x1660cd, FLASH_LOCK_LOW_2M_MID1660CD},
#elif defined(MCU_CORE_W92)
    //4M
    {0x166085, FLASH_LOCK_LOW_2M_MID166085}
#elif defined(MCU_CORE_TL521X)
    //1m
    {0x1460c8, FLASH_LOCK_LOW_512K_MID1460C8},
    {0x146085, FLASH_LOCK_LOW_512K_MID146085},
    //2M
    {0x1560c8, FLASH_LOCK_LOW_1M_MID1560C8},
    {0x156085, FLASH_LOCK_LOW_1M_MID156085},
    //4M
    {0x1660cd, FLASH_LOCK_LOW_2M_MID1660CD},
#else
    {0, 0}
#endif
};
#if FLASH_PROTECT_MODIFY_CONFIG
    #define list_fp flash_init_list
#else
    #define list_fp NULL
#endif


#if defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL323X)||defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL752X)

flash_hal_user_handler_t flash_handler = {
    .list      = list_fp,
    .flash_cnt = (sizeof(flash_init_list) / sizeof(flash_user_defined_list_t)),
};
#elif defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_W92)|| defined(MCU_CORE_TL522X)
flash_hal_user_handler_t flash_handler[SLAVE_CNT] = {
    {
     .list       = list_fp,
     .flash_cnt  = (sizeof(flash_init_list) / sizeof(flash_user_defined_list_t)),
     .slave_size = SLAVE_SIZE_64M,
     },
    {
     .list       = list_fp,
     .flash_cnt  = (sizeof(flash_init_list) / sizeof(flash_user_defined_list_t)),
     .slave_size = SLAVE_SIZE_NONE,
     },
    {
     .list       = list_fp,
     .flash_cnt  = (sizeof(flash_init_list) / sizeof(flash_user_defined_list_t)),
     .slave_size = SLAVE_SIZE_NONE,
     },
    {
     .list       = list_fp,
     .flash_cnt  = (sizeof(flash_init_list) / sizeof(flash_user_defined_list_t)),
     .slave_size = SLAVE_SIZE_NONE,
     },
};
#endif


#if defined(MCU_CORE_B91)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_B92)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, gpio_voltage_e gpio_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL7518)
void platform_init(vbat_type_e vbat_v, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL751X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL721X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL321X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_W92)
void platform_init(vbat_type_e vbat_v, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL322X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL323X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL753X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL521X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL711X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL523X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en)
#elif defined(MCU_CORE_TL522X)
void platform_init(unsigned char flash_protect_en)
#else
void platform_init(unsigned char flash_protect_en)
#endif
{
/**
    ===============================================================================
                         ##### sys_init #####
    ===============================================================================
*/
#if defined(MCU_CORE_B91)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_B92)
    sys_init(power_mode, vbat_v, gpio_v, cap);
#elif defined(MCU_CORE_TL7518)
    sys_init(vbat_v);
#elif defined(MCU_CORE_TL751X)
    sys_init(power_mode, vbat_v);
#elif defined(MCU_CORE_TL721X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_TL321X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_W92)
    sys_init(vbat_v);
#elif defined(MCU_CORE_TL322X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_TL323X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_TL753X)
    sys_init(power_mode, vbat_v);
#elif defined(MCU_CORE_TL521X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_TL711X)
    sys_init(power_mode, vbat_v);
#elif defined(MCU_CORE_TL523X)
    sys_init(power_mode, vbat_v, cap);
#elif defined(MCU_CORE_TL522X)
    sys_init();
#else
    sys_init();
#endif


#if (!defined(DUT_TEST))
   #if defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL521X)
    /**
        ===============================================================================
        To prevent leakage, all GPIOs are set to High-impedance and also enable the pull-down resistor except the MSPI pins and SWS.
        ===============================================================================
    */
    gpio_init(1);
   #else
    /**
        ===============================================================================
        To prevent leakage, all GPIOs are set to High-impedance except the MSPI pins and SWS.
        ===============================================================================
    */
    gpio_shutdown(GPIO_ALL);

   #endif
#if JTAG_MODE
    #if defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL522X) || defined(MCU_CORE_TL753X)
        jtag_set_pin_en();
    #endif
#endif
#endif

/**
    ===============================================================================
                         ##### update system status #####
    ===============================================================================
    After each initialization, you need to update the system status and set it to a fixed value.
    Otherwise, the next judgment may be inaccurate because the corresponding value is not configured.
    ===============================================================================
*/
#if defined(MCU_CORE_B92) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL523X)|| defined(MCU_CORE_TL522X) || defined(MCU_CORE_TL753X)
    pm_update_status_info(1);
#elif defined(MCU_CORE_TL752X)
    pm_update_status_info(0);
#endif

    /**
    ===============================================================================
                         ##### set SWS pull #####
    ===============================================================================
    If SWS is not connected to the analog pull up and down,it is in a suspended state and may be in a level indeterminate state,
    which will affect the electric leakage of digital and lead to higher current than normal during suspend sleep.
    there may be the risk of SWS miswriting the chip registers or sram causing a crash.
    ===============================================================================
*/
#if defined(MCU_CORE_TL752X)
    gpio_set_up_down_res(GPIO_SWS, GPIO_PULLUP);
#else
    gpio_set_up_down_res(GPIO_SWS, GPIO_PIN_PULLUP_1M);
#endif
/**
    ===============================================================================
                         ##### 32k watchdog stop #####
    ===============================================================================
    For chips with 32K watchdog function, the 32K watchdog function is enabled by default.
    If the program does not handle the watchdog, the chip will reset in 5s,
    and the driver will turn it off by default. If you want to use this function,
    you can replace this place with the dog feeder interface.
    ===============================================================================
*/
#if !defined(MCU_CORE_B91) && !defined(MCU_CORE_TL752X)
    wd_32k_stop();
#endif


    /**
    ===============================================================================
                        ##### timer watchdog stop #####
    ===============================================================================
    For chips with timer watchdog function, the timer watchdog function is enabled by startup.S.
    If the program does not handle the watchdog, the chip will reset in 10s,
    and the driver will turn it off by default. If you want to use this function,
    you can replace this place with the dog feeder interface.
    ===============================================================================
*/
#if !defined(MCU_CORE_TL752X)
    wd_stop();
#endif    
/**
    ===============================================================================
                         ##### calibration #####
    ===============================================================================
    This function can improve the performance of some modules,
    which is described in the function comments.
    Called immediately after sys_init,set in other positions,
    some calibration values may not take effect.
    ===============================================================================
*/
#if defined(MCU_CORE_B91)|| defined(MCU_CORE_TL523X)
    user_read_flash_value_calib();
#elif defined(MCU_CORE_B92)
    calibration_func(gpio_v);
#elif defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL752X) || defined(MCU_CORE_TL521X)
    calibration_func();
#endif

//The A0 chip's AVDD1/AVDD2/DVDD1/DVDD2 voltages will be on the high side and need to be set to the correct gear according to the following requirements:
//1.DVDD2>DVDD1>0.8V
//2.Plus or minus 10% of the design value is safe.
//#if defined(MCU_CORE_TL7518)
//  pm_set_avdd1(PM_AVDD1_VOLTAGE_1V050);//AVDD1 voltage select(LDO) 000:1.050V,design value:1.15V
//  pm_set_dvdd2(PM_DVDD2_VOLTAGE_0V750);//DVDD2 voltage select(LDO) 000:0.750V,design value:0.8V
//  pm_set_avdd2(PM_AVDD2_VOLTAGE_2V346);//AVDD2 voltage select(LDO) 000:2.346V,design value:1.8V
//  pm_set_dvdd1(PM_DVDD1_VOLTAGE_0V725);//DVDD1 voltage select(LDO) 000:0.725V,design value:0.8V
//#endif

//The A1 Chip consistency is poor, one is that some chips do not meet the DVDD2>=DVDD1>=0.8 this requirement,
//two is that some chips AVDD1/AVDD2 does not meet the theoretical design value (ldo power supply mode reference value: 1.04v/1.8v):
//When the performance test, the actual measurement of the default voltage, whether to adjust the corresponding voltage block to meet the above conditions.
//#if defined(MCU_CORE_TL751X)
//    if(g_chip_version==CHIP_VERSION_A1){
//        if (power_mode == LDO_AVDD_LDO_DVDD)
//        {
//            pm_set_avdd1(PM_AVDD1_VOLTAGE_1V075);                           //target 1.04
//            pm_set_avdd2(PM_AVDD2_REF_0V740, PM_AVDD2_VOLTAGE_1V890);       //target 1.8-1.98
//            pm_set_dvdd1(PM_DVDD1_VOLTAGE_0V825);                           //target 0.8
//            pm_set_dvdd2(PM_DVDD2_VOLTAGE_0V850);                           //target 0.8
//        }
//        else
//        {
//            pm_set_bk1(PM_BK1_TRIM_VOLTAGE_1V949, PM_BK1_ADJ_VOLTAGE_1V850);//target 1.8-1.98
//            pm_set_bk2(PM_BK2_3_4_VOLTAGE_1V04);                            //target 1.04
//            pm_set_bk3(PM_BK2_3_4_VOLTAGE_0V93);                            //target 0.8
//        }
//    }
//#endif

// The current DCDC voltage config is based on only two boards and hasn't been tested in large quantities. The actual power supply situation may vary with different chips, user needs to adjust the voltage levels based on their own board tests.
// When setting the voltage levels, you need to pay attention that in the circuit design, the highest bits of BK3 and BK4 are shared. If one is set to 1, both are considered 1.
#if defined(MCU_CORE_TL753X)
    if (power_mode == LDO_AVDD_LDO_DVDD)
    {
 #if A0_VERSION

#endif        
    } else {
#if A0_VERSION
        analog_write_reg8(0xb5, 0x87);
        pm_set_avdd1(PM_AVDD1_VOLTAGE_1V216);                            // test result 1.186 / target 1.2
        pm_set_avdd2(PM_AVDD2_VOLTAGE_2V083);                            // test result 2.138 / target 2.05
        pm_set_dvdd1(PM_DVDD1_VOLTAGE_0V884);                            // test result 0.844 / target 0.85
        pm_set_dvdd2(PM_DVDD2_VOLTAGE_0V858);                            // test result 0.841 / target 0.85

#endif        
    }
#endif
    /*
    * For the current A0 version, it is important to focus on whether the following voltage outputs meet expectations before testing, 
    * especially before conducting performance or stability tests.
    * A1 has fixed the issue of low voltage, so the trim code here was commented out.
    */
#if defined(MCU_CORE_TL721X)
    // pm_set_dig_ldo_voltage(DIG_LDO_TRIM_0P850V);
#endif

#if (defined(MCU_CORE_TL751X) && !defined(MCU_CORE_TL751X_N22) && DCDC_WORKAROUND_MODE)
    if (pm_get_dcdc_power() != 0x00) {
        lpc_set_input_chn(LPC_INPUT_PF6);
        lpc_set_input_ref(LPC_NORMAL, LPC_REF_820MV);
        lpc_set_scaling_coeff(LPC_SCALING_PER100);
        lpc_power_on();
        pm_set_wakeup_src(PM_WAKEUP_COMPARATOR);
        pm_clr_irq_status(FLD_WAKEUP_STATUS_ALL);
        plic_interrupt_enable(IRQ_PM_LVL);
        core_interrupt_enable();
    }
#endif

/**
    ===============================================================================
                        ##### driver sdk firmware protection #####
    ===============================================================================
    Flash write protection must be added, the size of the protected area is decided according to the application bin file,
    the principle is as follows:
    1.The program area is protected (to prevent the program area from being erased by mistake);
    2.the program will modify the data area is not protected (if write-protected, each time before erasing the need to unprotect,
      so that there is a risk that the status register of the flash has been mistakenly rewritten);

    @note if flash protection fails, LED1 lights up long, and keeps while.
    ===============================================================================
*/
#if !INTERNAL_SIMULATION_DEBUG
    #if (!defined(DUT_TEST) && defined(MCU_STARTUP_FLASH))
    if (flash_protect_en) {
        #if defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL323X)|| defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL752X)
        unsigned char flash_init_flag = hal_flash_init(&flash_handler);
        #elif defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_W92) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL522X) || defined(MCU_CORE_TL753X)
        unsigned char flash_init_flag = hal_flash_init((flash_hal_user_handler_t *)flash_handler);
        #else
        unsigned char flash_init_flag = 0;
        #endif
        if (flash_init_flag != 0) {
            //gpio_set_high_level(LED1);
            while (1)
                ;
        }

        #if defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL323X)||defined(MCU_CORE_TL521X)|| defined(MCU_CORE_TL523X) || defined(MCU_CORE_TL752X)
        unsigned char lock_flag = hal_flash_lock();
        #elif defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL522X) || defined(MCU_CORE_W92) || defined(MCU_CORE_TL711X)
        unsigned char lock_flag = hal_flash_lock_with_device_num(SLAVE0);
        #endif
        if (!(lock_flag == 1)) {
            //gpio_set_high_level(LED1);
            while (1)
                ;
        }
    }
    #else
    (void)flash_protect_en;
    #endif
#else
    (void)flash_protect_en;
#endif //#if defined(INTERNAL_SIMULATION_DEBUG)
}

#if (defined(MCU_CORE_TL751X) && !defined(MCU_CORE_TL751X_N22) && DCDC_WORKAROUND_MODE)
_attribute_ram_code_sec_noinline_ void pm_irq_handler(void)
{
    if (pm_get_wakeup_src() & (WAKEUP_STATUS_COMPARATOR)) {
        pm_powerdown_dcdc();
        pm_poweron_dcdc();
        //        gpio_function_en(LED2);
        //        gpio_output_en(LED2);
        //        gpio_input_dis(LED2);
        //        gpio_set_high_level(LED2);
    }
    pm_clr_irq_status(FLD_WAKEUP_STATUS_ALL);
}
PLIC_ISR_REGISTER(pm_irq_handler, IRQ_PM_IRQ)

_attribute_ram_code_sec_ void pm_level_irq_handler(void)
{
    if (pm_get_wakeup_src() & (WAKEUP_STATUS_COMPARATOR)) {
        pm_powerdown_dcdc();
        pm_poweron_dcdc();
        //        gpio_function_en(LED2);
        //        gpio_output_en(LED2);
        //        gpio_input_dis(LED2);
        //        gpio_set_high_level(LED2);
    }
    pm_clr_irq_status(FLD_WAKEUP_STATUS_ALL);
}
PLIC_ISR_REGISTER(pm_level_irq_handler, IRQ_PM_LVL)
#endif

#endif
