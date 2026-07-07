# component/net_cli_config - cross-platform network-diagnostic console commands: `resolve <host>` (DNS
# lookup) + `ip6` (list IPv6 addresses). Shared out of AmebaD's app_km4/wifi_cli.c (iwpriv stays there).
# Both platforms link net_cli + call net_cli_register() from app_console_register_platform.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(net_cli STATIC EXCLUDE_FROM_ALL
    net_cli.c
)
target_include_directories(net_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# FreeRTOS-CLI for the command registration; FreeRTOS-Plus-TCP for the resolver (FreeRTOS_getaddrinfo) +
# endpoint iteration + FreeRTOS_inet_ntop6. c-ares (cares_resolve.h) is borrowed only when the consumer
# stamps AT_PROJECT_CARES on net_cli + links cares_port (CARES=ON), mirroring sntp_time.
target_link_libraries(net_cli PUBLIC FreeRTOS freertos_cli freertos-plus-tcp_config)
