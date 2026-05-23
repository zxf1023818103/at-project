# bflb_os — FreeRTOS port + helpers + POSIX layer + power_mgmt
# (extracted from platform/bouffalo_sdk/components/os).
#
# Kernel C sources live in at-project/thirdparty/freertos-kernel (upstream V10.6.2).
# What's here is the Bouffalo-private port + glue:
#   - portable/GCC/RISC-V/{common,c906}/ — T-Head C906 RISC-V port
#   - portable/MemMang/heap_3.c — heap routed via components/mm
#   - freertos_port.c — Bouffalo weak hooks + vTaskHandleForeachFromISR stub
#   - freertos_helper/ — optional PS extend + section time stat
#   - posix/ — FreeRTOS-Plus-POSIX
#   - power_mgmt/ — Bouffalo tickless power management

set(UPSTREAM_FREERTOS ${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/freertos-kernel)
if(NOT EXISTS ${UPSTREAM_FREERTOS}/tasks.c)
    message(FATAL_ERROR "Upstream FreeRTOS-Kernel missing at ${UPSTREAM_FREERTOS}.")
endif()

# ---------- bflb_os: FreeRTOS kernel + Bouffalo port ----------
if(CONFIG_FREERTOS)
    set(BFLB_OS_SOURCES
        ${UPSTREAM_FREERTOS}/croutine.c
        ${UPSTREAM_FREERTOS}/event_groups.c
        ${UPSTREAM_FREERTOS}/list.c
        ${UPSTREAM_FREERTOS}/queue.c
        ${UPSTREAM_FREERTOS}/tasks.c
        ${UPSTREAM_FREERTOS}/timers.c
        ${UPSTREAM_FREERTOS}/stream_buffer.c
        ${CMAKE_CURRENT_LIST_DIR}/freertos_port.c
        ${CMAKE_CURRENT_LIST_DIR}/portable/MemMang/heap_3.c
        ${CMAKE_CURRENT_LIST_DIR}/portable/GCC/RISC-V/common/port.c
        ${CMAKE_CURRENT_LIST_DIR}/portable/GCC/RISC-V/common/portASM.S
    )

    if(CONFIG_PS_EXTEND)
        list(APPEND BFLB_OS_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/freertos_helper/ps_extend.c
            ${CMAKE_CURRENT_LIST_DIR}/portable/GCC/RISC-V/portStatTrap.c)
    endif()

    set(BFLB_OS_PUBLIC_INCLUDES
        ${UPSTREAM_FREERTOS}/include
        ${CMAKE_CURRENT_LIST_DIR}/portable/GCC/RISC-V/common
        ${CMAKE_CURRENT_LIST_DIR}/portable/GCC/RISC-V/common/chip_specific_extensions/RV32I_CLINT_no_extensions
    )
    if(CONFIG_PS_EXTEND)
        list(APPEND BFLB_OS_PUBLIC_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/freertos_helper/include)
    endif()

    if(CONFIG_POSIX)
        list(APPEND BFLB_OS_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_clock.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_mqueue.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_pthread_barrier.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_pthread_cond.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_pthread_mutex.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_pthread.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_sched.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_semaphore.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_timer.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_unistd.c
            ${CMAKE_CURRENT_LIST_DIR}/posix/source/FreeRTOS_POSIX_utils.c
        )
        if("${CPU_MODEL}" STREQUAL "b0")
            list(APPEND BFLB_OS_PUBLIC_INCLUDES
                ${CMAKE_CURRENT_LIST_DIR}/posix/include_n900
                ${CMAKE_CURRENT_LIST_DIR}/posix/include_n900/FreeRTOS_POSIX)
        else()
            list(APPEND BFLB_OS_PUBLIC_INCLUDES
                ${CMAKE_CURRENT_LIST_DIR}/posix/include
                ${CMAKE_CURRENT_LIST_DIR}/posix/include/FreeRTOS_POSIX)
        endif()
    endif()

    add_library(bflb_os STATIC ${BFLB_OS_SOURCES})
    target_include_directories(bflb_os PUBLIC ${BFLB_OS_PUBLIC_INCLUDES})

    # MTIME base address per chip (was sdk_add_compile_definitions in SDK)
    if("${CHIP}" STREQUAL "bl602" OR "${CHIP}" STREQUAL "bl702" OR "${CHIP}" STREQUAL "bl702l")
        set(_mtime "0x0200BFF8UL")
        set(_mtimecmp "0x02004000UL")
    elseif("${CHIP}" STREQUAL "bl618dg" AND NOT "${CPU_MODEL}" STREQUAL "a0")
        set(_mtime "0x18030000UL")
        set(_mtimecmp "0x18030008UL")
    else()
        set(_mtime "0xE000BFF8UL")
        set(_mtimecmp "0xE0004000UL")
    endif()
    target_compile_definitions(bflb_os PUBLIC
        configMTIME_BASE_ADDRESS=${_mtime}
        configMTIMECMP_BASE_ADDRESS=${_mtimecmp}
        portasmHANDLE_INTERRUPT=interrupt_entry
        default_interrupt_handler=freertos_risc_v_trap_handler
    )

    if(TARGET sdk_intf_lib)
        target_link_libraries(bflb_os PRIVATE sdk_intf_lib)
    endif()
endif()

# ---------- bflb_pwrmgmt: power management (optional) ----------
if(CONFIG_PWRMGMT)
    set(BFLB_PM_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/clock_manager.c
        ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/pm_manager.c
    )

    if(CONFIG_TICKLESS_EXT)
        list(APPEND BFLB_PM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/tickless.c
            ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/tickless_hook.c
            ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/rc32k_trim.c)
    endif()

    if(CONFIG_TICKLESS_ACCURATE)
        list(APPEND BFLB_PM_SOURCES ${CMAKE_CURRENT_LIST_DIR}/power_mgmt/tickless_accurate.c)
    endif()

    add_library(bflb_pwrmgmt STATIC ${BFLB_PM_SOURCES})
    target_include_directories(bflb_pwrmgmt PUBLIC ${CMAKE_CURRENT_LIST_DIR}/power_mgmt)

    if(CONFIG_TICKLESS_ACCURATE)
        target_compile_definitions(bflb_pwrmgmt PUBLIC
            LP_APP
            configUSE_TICKLESS_IDLE=1
            PM_PDS_LDO_LEVEL_SOC_DEFAULT=10
            PM_PDS_LDO_LEVEL_AON_DEFAULT=10
            CONFIG_LPFW_INIT_SKIP_COPY=1
            configEXPECTED_IDLE_TIME_BEFORE_SLEEP=40
            config_CUSTOM_TICKLESS=2
            configMTIME_BASE_ADDRESS=${_mtime}
            configMTIMECMP_BASE_ADDRESS=${_mtimecmp}
        )
        if(CONFIG_ACTIVE_CRYSTAL)
            target_compile_definitions(bflb_pwrmgmt PUBLIC CFG_ACTIVE_CRYSTAL)
        endif()
        if("${CHIP}" STREQUAL "bl616")
            if(CONFIG_DCDC_ENABLE)
                target_compile_definitions(bflb_pwrmgmt PUBLIC PM_PDS_LDO_LEVEL_RT_DEFAULT=8)
            else()
                target_compile_definitions(bflb_pwrmgmt PUBLIC PM_PDS_LDO_LEVEL_RT_DEFAULT=10)
            endif()
        endif()
    endif()

    if(TARGET sdk_intf_lib)
        target_link_libraries(bflb_pwrmgmt PRIVATE sdk_intf_lib)
    endif()
endif()
