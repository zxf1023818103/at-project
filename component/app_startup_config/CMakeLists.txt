# component/app_startup_config - shared startup helpers both platform main()s (src/main.cpp) call:
# LittleFS+env+TZ init, console + CLI registration, and the network-up service start, in ONE copy.
# The platform-specific parts come through weak seams strong-overridden in src/main.cpp (and
# component/c-ares_config for the DNS re-point). The symbols it calls (vfs_lfs_mount / *_cli_register /
# monitor_start) resolve at the final image link, so no link deps beyond FreeRTOS are needed.
add_library(app_startup STATIC EXCLUDE_FROM_ALL
    app_startup.c       # console + env/time (app_env_time_init / app_console_start)
    app_startup_net.c   # network-up (app_on_network_up) - separate TU so the console-less QUIC image
                        # can pull it without dragging in monitor_start / *_cli_register
)
target_include_directories(app_startup PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(app_startup PUBLIC FreeRTOS)
