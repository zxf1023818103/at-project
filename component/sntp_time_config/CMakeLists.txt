# component/sntp_time_config — cross-platform coreSNTP UTC time sync (the app glue over the
# upstream coreSNTP lib). An async SNTPv4 client: resolves the NTP server (NTP_SERVER env or
# DHCP option-42), syncs UTC over UDP (FreeRTOS-Plus-TCP), and feeds newlib's wall clock via
# amebad_set_unix_time (component/syscall_config time_base). Provides the strong sntp_sync_once()
# that overrides time_cli.c's weak stub (the `sntp_update` command + `date` showing real UTC).
# Started once via sntp_sync_start() from each platform's boot / network-up.
#
# The AmebaD HW-RTC mirror (rtc_time_store) and the newlib date self-test (time_selftest_run)
# are WEAK hooks — AmebaD app_km4 provides the strong defs; bl602 links no-ops.
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
