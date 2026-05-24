# bflb_power_mgmt — clock manager + power manager + (optional) tickless-idle
# stack. Extracted from bouffalo_sdk/components/os/power_mgmt.
#
# The SDK CMakeLists had four config flags:
#   CONFIG_TICKLESS_EXT       -> pulls in tickless.c / tickless_hook.c / rc32k_trim.c
#   CONFIG_TICKLESS_ACCURATE  -> pulls in tickless_accurate.c + a chunk of -D flags
#                                (LP_APP, configUSE_TICKLESS_IDLE=1, …) and the
#                                chip-specific MTIME / MTIMECMP base addresses
#   CONFIG_ACTIVE_CRYSTAL     -> adds -DCFG_ACTIVE_CRYSTAL
#   CONFIG_DCDC_ENABLE        -> only meaningful on bl616; flips PM_PDS_LDO_LEVEL_RT_DEFAULT
#
# All preserved here. The bl702 / bl702l / bl618dg MTIME branches have been
# dropped to match the umbrella's bl602/bl616/bl616cl-only scope.
#
# Heavy dep surface: pm_manager.c / clock_manager.c #include FreeRTOS.h,
# lwip headers, wifi_mgmr / btble / easyflash / macsw / board_rf headers.
# We only know how to link the chip layer ourselves; everything else has to
# come from consumer-side wiring (FreeRTOS + middleware) before this lib
# will actually compile clean.

add_library(bflb_power_mgmt STATIC
    clock_manager.c
    pm_manager.c
)

if(CONFIG_TICKLESS_EXT)
    target_sources(bflb_power_mgmt PRIVATE
        tickless.c
        tickless_hook.c
        rc32k_trim.c
    )
endif()

target_include_directories(bflb_power_mgmt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CONFIG_TICKLESS_ACCURATE)
    target_sources(bflb_power_mgmt PRIVATE tickless_accurate.c)

    target_compile_definitions(bflb_power_mgmt PUBLIC
        LP_APP
        configUSE_TICKLESS_IDLE=1
        PM_PDS_LDO_LEVEL_SOC_DEFAULT=10
        PM_PDS_LDO_LEVEL_AON_DEFAULT=10
        CONFIG_LPFW_INIT_SKIP_COPY=1
        configEXPECTED_IDLE_TIME_BEFORE_SLEEP=40
        config_CUSTOM_TICKLESS=2
    )

    if(CONFIG_ACTIVE_CRYSTAL)
        target_compile_definitions(bflb_power_mgmt PUBLIC CFG_ACTIVE_CRYSTAL)
    endif()

    if("${BOUFFALO_CHIP}" STREQUAL "bl616")
        if(CONFIG_DCDC_ENABLE)
            target_compile_definitions(bflb_power_mgmt PUBLIC PM_PDS_LDO_LEVEL_RT_DEFAULT=8)
        else()
            target_compile_definitions(bflb_power_mgmt PUBLIC PM_PDS_LDO_LEVEL_RT_DEFAULT=10)
        endif()
    endif()

    # MTIME base addresses for tickless_accurate.c.
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        set(_mtime_base    "0x0200BFF8UL")
        set(_mtimecmp_base "0x02004000UL")
    else()
        # bl616, bl616cl
        set(_mtime_base    "0xE000BFF8UL")
        set(_mtimecmp_base "0xE0004000UL")
    endif()
    target_compile_definitions(bflb_power_mgmt PUBLIC
        "configMTIME_BASE_ADDRESS=${_mtime_base}"
        "configMTIMECMP_BASE_ADDRESS=${_mtimecmp_base}"
    )
endif()

# Chip / HAL layer + FreeRTOS come via extracted components. lwip /
# wifi_mgmr / btble / easyflash / macsw / board_rf are still up to the
# consumer to provide.
target_link_libraries(bflb_power_mgmt PUBLIC
    bflb_lhal
    ${BOUFFALO_CHIP}_std
    bflb_sys
    bflb_freertos_port
)
