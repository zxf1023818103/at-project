# component/amebad/rtc_time_port — AmebaD (RTL8721D KM4) port of the shared RTC seam
# (component/rtc_time_config, rtc_time.h): strong rtc_time_store / rtc_time_seed over the Realtek
# RTC HAL (rtc_api, amebad_mbed) + a RRAM-kept year, giving the retained wall clock bl602's
# rtc_time_port provides over the HBN counter. main.cpp's AmebaD body calls rtc_time_seed() (a
# strong ref, so this .o is pulled from the archive) and sntp_time.c weak-calls rtc_time_store.
# Mirrors component/bouffalo_sdk/rtc_time_port. KM4-only.
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(rtc_time_port STATIC rtc_time_amebad.c)

# rtc_time.h (the shared seam) + rtc_api.h (amebad_mbed) + at_log.h (rxi log) + syscalls.h
# (amebad_set_unix_time, header-only here — the symbol resolves from the force-included
# syscall_config OBJECT lib at the image link).
target_link_libraries(rtc_time_port PUBLIC rtc_time)
target_link_libraries(rtc_time_port PRIVATE amebad_mbed amebad_sdk_includes rxi_log FreeRTOS)
target_include_directories(rtc_time_port PRIVATE
    "$<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>")
