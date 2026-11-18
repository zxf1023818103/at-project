# component/bouffalo_sdk/mbedtls_bench_port — RISC-V (bl602/bl616) cycle-timer override for
# the shared mbedtls_bench (component/mbedtls_bench_config). Supplies the STRONG bench_timer_*
# (mcycle CSR + bflb clock) that replaces the shared ARM SysTick default on RISC-V.
add_library(mbedtls_bench_port STATIC EXCLUDE_FROM_ALL
    bench_timer_bflb.c
)
# mbedtls_bench: the seam header (mbedtls_bench.h). bflb_lhal: bflb_clock.h (CPU clock API).
target_link_libraries(mbedtls_bench_port PRIVATE mbedtls_bench bflb_lhal)
