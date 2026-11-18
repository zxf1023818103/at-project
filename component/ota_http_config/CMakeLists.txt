# component/ota_http_config - the shared `ota_http <url>` OTA-over-HTTP(S) command: coreHTTP request
# builder + llhttp streaming into the OTA slot writer (ota_flash.h), over the shared TLS transport.
# Platform-neutral, but it reads per-image macros (AT_PROJECT_CARES for the DNS path), so - like
# src/main.cpp - the SOURCES are compiled into each platform's app (target_sources ${OTA_HTTP_SOURCES})
# rather than a prebuilt lib, so they pick up the app's macros.
#
# This INTERFACE lib carries only what ota_http_config OWNS: the urlparse.h include dir + the OTA slot
# interface (ota_flash.h, via ota_flash_config). The rest of ota_http.c's headers - coreHTTP/llhttp,
# transport_tls.h, FreeRTOS-CLI, rxi log, mbedTLS PSA, FreeRTOS-Plus-TCP - already sit on the app's
# include path (each app links those libs for its own OTA/AWS build), so ota_http.c compiled inside the
# app sees them without ota_http_config forcing a link (the variant transport libs aren't a single
# linkable target).
add_library(ota_http_config INTERFACE)
target_include_directories(ota_http_config INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ota_http_config INTERFACE ota_flash_config)

set(OTA_HTTP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ota_http.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/urlparse.c"
    CACHE INTERNAL "shared ota_http command sources (compiled per-app for per-image macros)")
