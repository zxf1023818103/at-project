# bl618dg_phyrf — INTERFACE wrapper around the prebuilt BL618DG PHY/RF static lib.
# Extracted from bouffalo_sdk/drivers/soc/bl618dg/phyrf.
#
# Upstream ships three variants; only the B0 one is vendored here because this
# project targets the mass-production B0 (Nuclei-900) part:
#   libbl618dg_phyrf_b0.a    B0  — ilp32d  (vendored, verified double-float ABI)
#   libbl618dg_phyrf.a       A-series — ilp32f + xtheade (NOT vendored)
#   libbl618dg_phyrf_4mfg.a  A-series manufacturing build (NOT vendored)
# Linking an A-series variant into a B0 image fails outright:
# "can't link single-float modules with double-float modules".

add_library(bl618dg_phyrf INTERFACE)

target_include_directories(bl618dg_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(bl618dg_phyrf INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/lib-gcc_10.2.0-toolchain_V2.6.1/libbl618dg_phyrf_b0.a"
)
