# component/iperf_config — cross-platform iperf2-protocol throughput probes over FreeRTOS-Plus-TCP
# raw sockets (iperf_{client,server,udp_client,udp_server}) + the `iperf` console command wrapper.
# Homegrown (static TX buffer, no iperf3 mmap/pthread/cJSON). AmebaD auto-runs iperf_server_start()
# from its network-up hook (make IPERF=ON); bl602 registers the console command (iperf_cli_register).
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames, not full paths.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(iperf STATIC EXCLUDE_FROM_ALL
    iperf_client.c
    iperf_server.c
    iperf_udp_client.c
    iperf_udp_server.c
    iperf_cli.c
)
target_include_directories(iperf PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")   # iperf.h
# raw FreeRTOS-Plus-TCP sockets (FreeRTOS_socket/recv/send) + the CLI core + rxi log (at_log.h).
target_link_libraries(iperf PUBLIC
    FreeRTOS
    freertos-plus-tcp_config
    freertos_cli
    rxi_log
)
