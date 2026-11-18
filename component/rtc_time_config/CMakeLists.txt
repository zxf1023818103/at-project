# component/rtc_time_config - the shared RTC seam (rtc_time.h): rtc_time_store (sntp_time.c mirrors
# UTC into the HW RTC) + rtc_time_seed (boot-time seed from the RTC). Header-only INTERFACE lib: each
# platform provides the strong implementation in its own port (AmebaD app_km4/rtc_time.c over
# rtc_api/RRAM; Bouffalo component/bouffalo_sdk/rtc_time_port over bflb_rtc + KV). No weak-default
# archive here on purpose - a weak def in a static lib can be bound BEFORE the real port is scanned
# (leaving the no-op); instead sntp_time.c weak-references rtc_time_store, and each main() calls
# rtc_time_seed() only where a port is linked.
add_library(rtc_time INTERFACE)
target_include_directories(rtc_time INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
