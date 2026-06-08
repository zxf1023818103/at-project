#-------------------------------------------------------------------------------
# RTL8721D (Ameba-D) KM4 — TF-M out-of-tree platform CMake.
#
# Minimal bring-up (BL2 OFF / TF-M = IMG3, isolation L1). Models the Arm AN521
# platform but drops the MCUboot/BL2 wiring and the AN521-specific MMIO drivers.
# The SPE scatter file is the TF-M common GCC linker, parameterised by our
# partition/region_defs.h.
#
# Files still TODO (configure-driven, add each then re-run cmake):
#   cmsis_core/rtl8721d_device.h  — IRQn_Type enum (from fwlib/rtl8721d_vector.h)
#                                    + M33 config (__SAUREGION_PRESENT=1,
#                                    __FPU_PRESENT=1, __DSP_PRESENT=1,
#                                    __NVIC_PRIO_BITS, ...) then #include core_cm33.h
#   cmsis_core/cmsis.h           — #include "rtl8721d_device.h"
#   cmsis_core/startup_rtl8721d.c — M33 secure vector table + Reset_Handler
#   cmsis_core/system_core_init.c — SystemInit() (clocks already up via Ameba boot)
#   tfm_hal_platform.c           — tfm_hal_platform_init() (calls target_cfg fns + UART)
#   tfm_hal_isolation.c          — isolation L1 boundaries (MPU optional at L1)
#   cmsis_drivers/Driver_USART.c — Driver_USART0 (TF-M secure log over LOGUART)
#   cmsis_drivers/Driver_Flash.c — Driver_FLASH0 (ITS/PS)
#   tfm_peripherals_def.h, mmio_defs.h
#-------------------------------------------------------------------------------
cmake_policy(SET CMP0076 NEW)
set(CMAKE_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR})

#-------------------- region defs (partition/) ---------------------------------
target_include_directories(platform_region_defs
    INTERFACE
        partition
)

#-------------------- secure platform library ---------------------------------
# target_cfg.c programs the SAU (core_cm33.h gates SAU behind __ARM_FEATURE_CMSE
# ==3, i.e. -mcmse), so the secure platform lib must be CMSE-compiled.
target_compile_options(platform_s
    PUBLIC
        ${COMPILER_CMSE_FLAG}
)

target_include_directories(platform_s
    PUBLIC
        .
        partition
        cmsis_core
        cmsis_drivers
)

target_sources(platform_s
    PRIVATE
        target_cfg.c
        tfm_hal_platform.c
        tfm_hal_isolation.c
        cmsis_core/system_core_init.c
        $<$<BOOL:${TFM_PARTITION_PLATFORM}>:services/src/tfm_platform_system.c>
        cmsis_drivers/Driver_USART.c
        cmsis_drivers/Driver_Flash.c
)

#-------------------- secure image: startup + scatter -------------------------
target_sources(tfm_s
    PRIVATE
        cmsis_core/startup_rtl8721d.c
)

target_add_scatter_file(tfm_s
    $<$<C_COMPILER_ID:GNU>:${PLATFORM_DIR}/ext/common/gcc/tfm_common_s.ld>
)

#-------------------- non-secure platform (NS interface only) ------------------
if(TARGET platform_ns)
    target_include_directories(platform_ns
        PUBLIC
            .
            partition
            cmsis_core
    )
endif()
