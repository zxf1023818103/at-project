# component/wifi_service_config — the shared, cross-platform WiFi provisioning + reconnect
# policy (wifi_service.c) driven over the wifi_port HAL (wifi_port.h). ONE copy of the connect
# strategy / channel cache / offline guard / reconnect monitor / `wifi forget` for every platform;
# each platform supplies its own wifi_port_* implementation (Bouffalo wifi4 / Realtek RTK) where
# its driver headers already resolve (in src/main.cpp under the platform #ifdef).
# rxi/printf log lines print __FILE__: map this dir's prefix away so they show basenames.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(wifi_service STATIC EXCLUDE_FROM_ALL
    wifi_service.c
    wifi_cli.c
)
# wifi_service.h + wifi_port.h for both the shared policy consumers and the platform ports.
target_include_directories(wifi_service PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# FreeRTOS tasks + event groups; the `wifi` console command uses FreeRTOS-Plus-CLI (freertos_cli).
# env (getenv/setenv/env_save) comes from syscall_config at the final image link (the same newlib
# retarget both platforms use), so no link dep is needed for it here.
target_link_libraries(wifi_service PUBLIC FreeRTOS freertos_cli)
