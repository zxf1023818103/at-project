# component/bouffalo_sdk/rtc_time_port — Bouffalo (BL602/BL616/BL616CL) port of the shared RTC seam
# (component/rtc_time_config, rtc_time.h): strong rtc_time_store / rtc_time_seed over the bflb_rtc
# HBN-domain counter + a KV-persisted reference, giving bl602 the same retained wall clock AmebaD's
# rtc_time.c provides over the Realtek RTC. Linked into the bouffalo_sdk umbrella; main() calls
# rtc_time_seed() (a strong ref, so this .o is pulled) and sntp_time.c weak-calls rtc_time_store.
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(rtc_time_port STATIC rtc_time_bl602.c)

# rtc_time.h (the shared seam); bflb_rtc.h / bflb_core.h (the HBN RTC HAL); at_log.h (the single
# project logger, bflb_log is gone). syscalls.h (amebad_set_unix_time) is header-only here - the
# symbol resolves from the force-included syscall_config OBJECT lib at the image link.
target_link_libraries(rtc_time_port PUBLIC rtc_time)
target_link_libraries(rtc_time_port PRIVATE bflb_lhal rxi_log)
target_include_directories(rtc_time_port PRIVATE
    "$<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>")
