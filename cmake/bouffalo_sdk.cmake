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

# bflb_add_executable() — BL chip ELF/.bin pipeline + auto .map. Bundled
# here because it is part of the same "BL chip toolchain build" contract
# as the umbrella below; consuming projects should not need a separate
# include() for it.
include(${AT_PROJECT_ROOT}/cmake/bflb_elf_to_bin.cmake)

add_subdirectory(${AT_PROJECT_ROOT}/component/bouffalo_sdk
                 ${CMAKE_BINARY_DIR}/component/bouffalo_sdk)

# Auto-close the link group bouffalo_sdk's INTERFACE link options open.
# `-Wl,--end-group` is smuggled in as a library entry so CMake appends
# it to INTERFACE_LINK_LIBRARIES (which gets emitted AFTER the link
# options block where --start-group sits, and after every archive the
# umbrella transitively pulls). Putting it in target_link_options would
# lump it together with --start-group before any libraries — wrong slot,
# group never opens.
#
# The cmake_language(DEFER) call schedules the append for the very end
# of the top-level project's CMakeLists.txt scope. That ordering matters:
# external libs (cjson / lwip / mbedtls / …) stitch themselves into
# bouffalo_sdk's INTERFACE_LINK_LIBRARIES from their own cmake/<name>.cmake
# glue (auto-enumerated by component/CMakeLists.txt). Deferring to project
# scope guarantees `-Wl,--end-group` is the very last entry in the list,
# past every archive ld needs to resolve.
#
# Consumer side is now a one-liner: target_link_libraries(<exe> PRIVATE bouffalo_sdk).
# No finalize call needed.
function(_bouffalo_sdk_close_link_group)
    target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR}
    CALL _bouffalo_sdk_close_link_group)
