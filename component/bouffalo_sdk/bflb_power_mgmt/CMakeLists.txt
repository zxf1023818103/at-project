# bflb_power_mgmt — clock manager + power manager + (optional) tickless-idle
# stack. Extracted from bouffalo_sdk/components/os/power_mgmt.
#
# Config flags:
#   CONFIG_PM_MANAGER         -> pulls in pm_manager.c + clock_manager.c
#                                (NEW — see comment below; default OFF on our
#                                tree because both files are dead weight for the
#                                current scope and pm_manager.c hard-depends on
#                                wifi6's wifi_mgmr_ext.h which isn't extracted).
#   CONFIG_TICKLESS_EXT       -> pulls in tickless.c / tickless_hook.c / rc32k_trim.c
#   CONFIG_TICKLESS_ACCURATE  -> pulls in tickless_accurate.c + a chunk of -D flags
#                                (LP_APP, configUSE_TICKLESS_IDLE=1, …) and the
#                                chip-specific MTIME / MTIMECMP base addresses.
#                                NOTE: tickless_accurate.c calls pm_sleep_check_dispatch()
#                                from pm_manager.c, so CONFIG_TICKLESS_ACCURATE
#                                requires CONFIG_PM_MANAGER too.
#   CONFIG_ACTIVE_CRYSTAL     -> adds -DCFG_ACTIVE_CRYSTAL
#   CONFIG_DCDC_ENABLE        -> only meaningful on bl616; flips PM_PDS_LDO_LEVEL_RT_DEFAULT
#
# The bl702 / bl702l / bl618dg MTIME branches have been dropped to match the
# umbrella's bl602/bl616/bl616cl-only scope.
#
# Heavy dep surface (gated): pm_manager.c #include's lwip + <wifi_mgmr_ext.h>
# (wifi6/fhost header); clock_manager.c #include's "easyflash.h" + "bl_lp.h".
# Until wifi6 is extracted we keep CONFIG_PM_MANAGER off and the lib only
# carries the optional tickless* sources. Audit done at the time of gating:
# `pm_manager_*` / `clock_source_read` had zero callers in the extracted tree
# (only consumer was tickless_accurate.c, which is already gated).

add_library(bflb_power_mgmt STATIC empty.c)

if(CONFIG_PM_MANAGER)
    target_sources(bflb_power_mgmt PRIVATE
        clock_manager.c
        pm_manager.c
    )
    # pm_manager.c pulls in <lwip/tcpip.h> (needs rework for FreeRTOS-Plus-TCP),
    # <wifi_mgmr_ext.h>. Pick the wifi mgmr by CONFIG_* (TARGET check would be
    # order-dependent on add_subdirectory). This whole block is gated CONFIG_PM_MANAGER
    # (currently off); lwipcore was removed with the lwIP stack.
    #
    # clock_manager.c persists RC-cal/clock-source via easyflash's ef_get/set_env_blob
    # (binary KV), but component/lfs_easyflash was DELETED (Bouffalo now uses POSIX env
    # over /KV, string-only). Before enabling CONFIG_PM_MANAGER, port clock_manager.c's
    # two blob call sites to the VFS (open + read/write "/KV/<key>" via syscall_config).
    if(CONFIG_WL80211)
        target_link_libraries(bflb_power_mgmt PRIVATE wl80211)
    elseif(CONFIG_WIFI4)
        target_link_libraries(bflb_power_mgmt PRIVATE wifi4_manager)
    endif()
endif()

if(CONFIG_TICKLESS_EXT)
    target_sources(bflb_power_mgmt PRIVATE
        tickless.c        # vApplicationSleep (the tickless idle hook) + tickless_enter/exit
        tickless_hook.c   # tickless_hooks_register/get (NULL-safe if the app never registers)
        rc32k_trim.c      # empty TU on BL616 (its body is #if BL616CL||BL618DG); kept for parity
    )
    # tickless.c pulls the macsw/fhost WiFi + lp_fw header chain (wifi_mgmr_ext.h -> mac_types.h ->
    # macsw.h -> phy.h), the same one macsw_plat/fhost resolve. Mirror those include dirs + the SDK
    # umbrella libs so the branch compiles and its externs (macswl_ps_* / bl_lp_* / bl_pm_* / lpfw_cfg
    # / wifi_mgmr_*) resolve at the final image link. bflb_lhal / ${CHIP}_std / bflb_sys / freertos_port
    # are already PUBLIC-linked below.
    set(_wifi6 "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/wifi6")
    target_include_directories(bflb_power_mgmt PRIVATE
        "${_wifi6}/fhost/include"                                          # wifi_mgmr_ext.h (fhost variant)
        "${_wifi6}/macsw_os_adapter/include"                               # rtos_al.h / platform_al.h
        "${AT_ROOT}/platform/bouffalo_sdk/components/wireless/macsw/inc")  # macsw.h / mac_types.h / phy.h
    target_link_libraries(bflb_power_mgmt PRIVATE
        bflb_autoconf                 # -include autoconf.h (BL616 / CONFIG_*)
        bflb_sys                      # bl_lp.h (lpfw_cfg / bl_lp_* / bl_pm_*)
        "${BOUFFALO_CHIP}_phyrf"      # phy.h / wl_api.h (pulled by macsw.h)
        freertos_config)              # FreeRTOS.h / task.h
    # For sleep/abort/wake tracing on HW, add TICKLESS_DEBUG=1 here (prints "[TICKLESS]: ..." from
    # tickless.c). Left off by default (the per-attempt logs are noisy and would themselves keep the
    # UART/CPU busy). Confirmed on HW: with `tickless 1` the device deep-sleeps (console goes quiet,
    # PDS15 does not wake on UART) while STAYING associated to the AP (3+ min uptime, no deauth).

    # NIMBLE=ON: BLE controller arbitration on tickless.c's hook seam (the SDK's version lives in
    # tickless_accurate.c which we can't compile — vendor-kernel + pm_manager deps). Registered from
    # main.cpp via tickless_ble_hooks_init(); a no-op until btble_controller_init() runs.
    if(AT_ENABLE_NIMBLE)
        target_sources(bflb_power_mgmt PRIVATE tickless_ble.c)
        target_link_libraries(bflb_power_mgmt PRIVATE
            btblecontroller)          # btble_lib_api.h (btble_controller_sleep/_restore) + prebuilt .a
    endif()
endif()

# LP_FW=ON (CONFIG_LPAPP), without the full CONFIG_PM_MANAGER: compile ONLY clock_manager.c for
# app_clock_init() — the F32K / RC32K startup bring-up that clocks the HBN RTC so a PDS15 sleep can
# self-wake on its RTC compare (wl_lp runs this from main(); without it bl_lp_fw_enter() sleeps but the
# RTC never counts). The task-based path (CONFIG_CLOCK_SOURCE_EF_PARAM left undefined) uses NONE of the
# easyflash KV persistence — every ef_*_env_blob call site is under that flag — so there is no easyflash
# link dependency; only the top-level `#include "easyflash.h"` needs the header on the path. NOTE:
# app_clock_init() BLOCKS on a semaphore, so it must be called from a task AFTER the scheduler starts
# (main.cpp spawns lp_clock_init_task), never from the pre-scheduler app_platform_boot_init path.
if(CONFIG_LPAPP AND NOT CONFIG_PM_MANAGER)
    # lp_recovery.c — the bl_lp sys-exit (post-PDS15-wake) restore callback. The SDK contract for
    # the minimal tickless.c is that the APP registers this (every lp demo's lp_exit does); without
    # it the first real resume comes back to a dead board (no clocks/tick/console re-init).
    target_sources(bflb_power_mgmt PRIVATE clock_manager.c lp_recovery.c)
    target_include_directories(bflb_power_mgmt PRIVATE
        "${AT_ROOT}/platform/bouffalo_sdk/components/fs/littlefs/easyflash_port")  # easyflash.h (header only)
    target_link_libraries(bflb_power_mgmt PRIVATE
        bflb_autoconf                 # -include autoconf.h (BL616 / CONFIG_*)
        bflb_sys                      # bl_lp.h
        "${BOUFFALO_CHIP}_std"        # HBN_* / AON_* / pm_* 32K-clock HW driver funcs
        "${BOUFFALO_CHIP}_board_config"  # board.h (board_recovery) + board_rf.h (board_rf_ctl)
        freertos_config)              # FreeRTOS.h / task.h / semphr.h
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
    freertos_port
)
