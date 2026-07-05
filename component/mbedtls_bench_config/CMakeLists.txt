# component/mbedtls_bench_config — cross-platform upstream Mbed TLS v3.6.4 microbenchmark
# (mbedtls_bench() -> [MCB] scores) + the `bench` console command. The crypto is pure upstream
# mbedTLS; the only platform-specific piece is the cycle timer, behind the bench_timer_* seam:
# the shared mbedtls_bench.c carries the WEAK ARM Cortex-M SysTick default (AmebaD KM4), and
# component/bouffalo_sdk/mbedtls_bench_port supplies the STRONG RISC-V mcycle override (bl602).
#
# mbedcrypto's objects are archived into the IMAGE, not this static lib (AmebaD: the raw archives
# ride the CRYPTOBENCH cross_refs LINK_GROUP for the mbedtls internal circular refs; Bouffalo: the
# bouffalo_sdk umbrella, where WiFi WPA already pulls mbedcrypto). Here we only need the *compile*
# context — the platform's mbedTLS config surface (public headers + the shared mbedtls_user_config.h
# + the MBEDTLS_*_ALT include wiring) — which propagates as transitive usage requirements when we
# link mbedtls_config, so the bench TUs compile identically to the library on BOTH platforms.
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames, not full paths.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(mbedtls_bench STATIC EXCLUDE_FROM_ALL
    mbedtls_bench.c
    bench_cli.c
)
target_include_directories(mbedtls_bench PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")   # mbedtls_bench.h

# mbedtls_config (upstream umbrella: mbedcrypto/mbedx509/mbedtls + the FreeRTOS threading-alt):
# propagates the platform's mbedTLS config as transitive usage requirements — the SAME pattern
# component/esp_crt_bundle_config uses to compile mbedtls-using code cross-platform. On Bouffalo
# the config lives on mbedtls_cfg (PUBLIC-linked onto mbedcrypto), which a non-transitive genex on
# mbedcrypto would MISS, so link the umbrella instead. Also supplies mbedtls_threading_freertos_init().
# FreeRTOS (pvPortMalloc/tick) + the CLI core + rxi log (at_log.h).
target_link_libraries(mbedtls_bench PUBLIC
    mbedtls_config
    FreeRTOS
    freertos_cli
    rxi_log
)
