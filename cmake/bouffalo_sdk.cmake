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

# Close the linker group opened on bouffalo_sdk's INTERFACE link options +
# write a .map. Call this from the consuming project AFTER all external
# libs (cjson / lwip / mbedtls / …) have stitched themselves into the
# umbrella from their own cmake/<name>.cmake — the only ordering that puts
# `-Wl,--end-group` past every archive ld needs to resolve.
#
# Idempotent on the umbrella side (--end-group goes in exactly once via a
# GLOBAL property guard) but per-executable on the consumer side (every
# linked target gets its own -Wl,-Map=<target>.map).
function(bouffalo_sdk_finalize_link target)
    target_link_options(${target} PRIVATE
        -Wl,-Map=$<TARGET_FILE_BASE_NAME:${target}>.map
    )
    get_property(_grp_closed GLOBAL PROPERTY BOUFFALO_SDK_GROUP_CLOSED)
    if(NOT _grp_closed)
        # `-Wl,--end-group` smuggled in as a library entry so CMake appends
        # it to INTERFACE_LINK_LIBRARIES (which gets emitted AFTER the link
        # options block where --start-group sits, and after every archive
        # the umbrella transitively pulls). Putting it in
        # target_link_options would lump it together with --start-group
        # before any libraries — wrong slot, group never opens.
        target_link_libraries(bouffalo_sdk INTERFACE -Wl,--end-group)
        set_property(GLOBAL PROPERTY BOUFFALO_SDK_GROUP_CLOSED TRUE)
    endif()
endfunction()
