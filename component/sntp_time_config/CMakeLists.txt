# component/sntp_time_config — cross-platform coreSNTP UTC time sync (the app glue over the
# upstream coreSNTP lib). An async SNTPv4 client: resolves the NTP server (NTP_SERVER env or
# DHCP option-42), syncs UTC over UDP (FreeRTOS-Plus-TCP), and feeds newlib's wall clock via
# amebad_set_unix_time (component/syscall_config time_base). Provides the strong sntp_sync_once()
# that overrides time_cli.c's weak stub (the `sntp_update` command + `date` showing real UTC).
# Started once via sntp_sync_start() from each platform's boot / network-up.
#
# The AmebaD HW-RTC mirror (rtc_time_store) and the newlib date self-test (time_selftest_run)
# are WEAK hooks — sntp_time.c calls them only if a strong def is present, else no-op.
# time_selftest.c lives in THIS dir (a shared, platform-neutral newlib date/time demo: newlib +
# rxi at_log.h only, no SDK deps). It is NOT compiled into the sntp_time archive on purpose: a weak
# ref won't pull a strong def out of an archive, so a consumer that wants the demo compiles it as a
# DIRECT source of its executable (AmebaD app_km4 does; any platform can add the same target_sources).
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames, not full paths.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(sntp_time STATIC EXCLUDE_FROM_ALL sntp_time.c)
target_include_directories(sntp_time PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")   # sntp_time.h

# Header + symbol deps (the archives ride into whichever image links sntp_time):
#   freertos-plus-tcp_config  FreeRTOS_IP/Sockets/DNS.h + ulDHCPNtpServerAddress (DHCP opt-42 patch)
#   coresntp_config           core_sntp_client/serializer.h
#   backoffalgorithm_config   backoff_algorithm.h (retry backoff-with-jitter)
#   rxi_log                   at_log.h (log_info / log_error) — the single project logger (bflb_log deleted)
target_link_libraries(sntp_time PUBLIC
    FreeRTOS
    freertos-plus-tcp_config
    coresntp_config
    backoffalgorithm_config
    rxi_log
)
# syscall_config is an OBJECT lib (force-included at the image link) — take only its header
# (syscalls.h: amebad_set_unix_time); the symbol resolves at the image link.
target_include_directories(sntp_time PRIVATE
    "$<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>")
