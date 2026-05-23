# bflb_sysinit — extracted from platform/bouffalo_sdk/components/sysinit.
# Standard CMake; no SDK-private macros.

add_library(bflb_sysinit STATIC sysinit.c)

target_include_directories(bflb_sysinit PUBLIC ${CMAKE_CURRENT_LIST_DIR})

# When built inside bouffalo_sdk, inherit toolchain flags + CHIP defines + cross
# include paths from sdk_intf_lib (an INTERFACE target the SDK exposes).
# When built standalone (e.g. host unit test), the dep is absent and we just
# compile against whatever the caller provides.
if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_sysinit PRIVATE sdk_intf_lib)
endif()
