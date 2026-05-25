# Bouffalo-specific subtree. Entered only when BOUFFALO_CHIP is set (the
# root CMakeLists.txt gates this add_subdirectory). Cross-platform pieces
# (component/thridparty/, src/) live up at the root instead.
#
#   bouffalo_sdk         the Bouffalo Lab SDK extraction (HAL + chip-std
#                        + sysinit + port adapters + wifi/BT/RF stacks).
#                        Umbrella INTERFACE target `bouffalo_sdk` is what
#                        consumers link. Also pulls in upstream lwip
#                        (via bflb_lwip_port) and upstream mbedtls (after
#                        the three bflb_mbedtls_* port adapters).
#   lwip_dhcpd           Bouffalo's raw DHCP-server-on-lwip app, lifted
#                        out of the lwip submodule so it can depend on
#                        the upstream-clean lwip + freertos_kernel.
#   lfs_easyflash        EasyFlash KV API on top of upstream littlefs.
#   lfs_image            host-side helper to pack a directory tree into
#                        a LittleFS image (component/lfs_image/CMakeLists.txt
#                        is dual-mode: registers lfs_image_add() here,
#                        becomes a standalone host build when invoked
#                        via ExternalProject_Add from inside that helper).

# ---------------------------------------------------------------------
# bouffalo_sdk
# ---------------------------------------------------------------------
#
# Target chip selection. Typical usage is to set this through a chip
# toolchain file (cmake/bl602-toolchain.cmake etc., which also pins
# -march / -mabi / sysroot) but a plain -DBOUFFALO_CHIP=bl602 on the
# cmake command line works too — the CACHE STRING declaration here
# makes the value visible in cmake-gui / ccmake and the STRINGS
# property gives a dropdown of supported chips. set() with CACHE does
# not overwrite an existing value, so this is a no-op when the
# toolchain file has already set it.
set(BOUFFALO_CHIP "" CACHE STRING
    "Bouffalo target chip — required (bl602 | bl616 | bl616cl)")
set_property(CACHE BOUFFALO_CHIP PROPERTY STRINGS
    bl602 bl616 bl616cl)
if(NOT BOUFFALO_CHIP)
    message(FATAL_ERROR
        "BOUFFALO_CHIP is not set — either include a chip toolchain "
        "(-DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/bl602-toolchain.cmake or "
        "bl616 / bl616cl) or pass -DBOUFFALO_CHIP=<chip> directly.")
endif()
if(NOT BOUFFALO_CHIP MATCHES "^(bl602|bl616|bl616cl)$")
    message(FATAL_ERROR
        "BOUFFALO_CHIP='${BOUFFALO_CHIP}' is not a supported chip. "
        "Expected one of: bl602, bl616, bl616cl.")
endif()

# ASM is an SDK concern (startup code, FreeRTOS port asm, vendor archive
# stubs — 700+ .S files in components/bouffalo_sdk + freertos-kernel),
# not a user-application one. Enable it here so projects can keep their
# project() line to the languages they actually write (C / CXX).
#
# bflb_add_executable() (the ELF -> flashable BIN pipeline) is provided
# by component/bouffalo_sdk/bflb_image_tool/CMakeLists.txt, pulled in
# transitively by add_subdirectory(bouffalo_sdk).
enable_language(ASM)

# Third-party submodules are added by the root CMakeLists.txt before we
# get here, so zlibstatic / cjson / littlefs are already real targets that
# bouffalo_sdk's umbrella can pull into its INTERFACE link list.
add_subdirectory(bouffalo_sdk)

# Auto-close the link group bouffalo_sdk's INTERFACE link options open.
# `-Wl,--end-group` is smuggled in as a library entry so CMake appends
# it to INTERFACE_LINK_LIBRARIES (which gets emitted AFTER the link
# options block where --start-group sits, and after every archive the
# umbrella transitively pulls). Putting it in target_link_options would
# lump it together with --start-group before any libraries — wrong slot,
# group never opens.
#
# The cmake_language(DEFER) call schedules the append for the very end
# of the top-level project's CMakeLists.txt scope. Deferring to project
# scope guarantees `-Wl,--end-group` is the very last entry in the list,
# past every archive ld needs to resolve.
#
# Consumer side is a one-liner: target_link_libraries(<exe> PRIVATE bouffalo_sdk).
# No finalize call needed.
function(_bouffalo_sdk_close_link_group)
    target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
    CALL _bouffalo_sdk_close_link_group)

# ---------------------------------------------------------------------
# lwip_dhcpd — Bouffalo's raw-DHCP-server-on-lwip app. Lives at this
# level (not inside bouffalo_sdk) because it depends on the upstream
# lwip target `lwipcore`, defined by the bflb_lwip_port block.
# ---------------------------------------------------------------------
add_subdirectory(lwip_dhcpd)
target_link_libraries(bouffalo_sdk INTERFACE lwip_dhcpd)

# ---------------------------------------------------------------------
# lfs_easyflash — EasyFlash KV API on top of upstream littlefs. Hard
# prereqs (already registered by this point):
#   littlefs            from thridparty/
#   bflb_littlefs_port  from bouffalo_sdk
#   freertos_kernel + bflb_log
# ---------------------------------------------------------------------
add_subdirectory(lfs_easyflash)
target_link_libraries(bouffalo_sdk INTERFACE lfs_easyflash)

# ---------------------------------------------------------------------
# lfs_image — host-side LittleFS image builder. Dual-mode CMakeLists:
# add_subdirectory here registers the helper function lfs_image_add();
# the standalone host sub-build is launched separately via
# ExternalProject_Add when a consumer calls lfs_image_add().
# ---------------------------------------------------------------------
add_subdirectory(lfs_image)
