include_guard()

# Adds component/bouffalo_sdk (the Bouffalo Lab SDK extraction: HAL +
# chip-std + sysinit + port adapters + wifi/BT/RF stacks). The umbrella
# INTERFACE target `bouffalo_sdk` is what consumers actually link.
#
# Idempotent — wired by component/CMakeLists.txt's auto-enumeration AND
# safe to include() directly from per-chip cmake/*.cmake glue that needs
# bflb_*_port headers before the upstream submodule wrappers run
# (cmake/lwip.cmake / cmake/mbedtls.cmake / cmake/lfs_easyflash.cmake).

if(NOT DEFINED AT_PROJECT_ROOT)
    message(FATAL_ERROR "AT_PROJECT_ROOT must be set before including cmake/bouffalo_sdk.cmake")
endif()

add_subdirectory(${AT_PROJECT_ROOT}/component/bouffalo_sdk
                 ${CMAKE_BINARY_DIR}/component/bouffalo_sdk)
