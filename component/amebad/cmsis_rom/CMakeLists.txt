# component/amebad/cmsis_rom — AmebaD CMSIS: the ARM Cortex-M core headers
# (core_cm*/core_armv8m*/cmsis_gcc/cmsis_compiler...), the CMSIS-DSP arm_math
# headers, and the Realtek MPU HAL (mpu.h / mpu_config*.h). Relocated here from
#   sdk_headers/component/soc/realtek/amebad/cmsis/
# so the CMSIS headers and the few CMSIS pieces that are mask-ROM resident live
# together.
#
#   amebad_cmsis_rom (INTERFACE)   exposes the headers. amebad_sdk_includes links
#                                  it, so every consumer keeps getting them.
#   cmsis_rom_km{4,0}.ld           the mask-ROM addresses of the ROM-resident CMSIS
#                                  bits: mpu_* (MPU HAL) on both cores, plus the
#                                  CMSIS-DSP FFT tables (armBitRevIndexTable1024 /
#                                  twiddleCoef_1024) on KM4. Partitioned out of the
#                                  per-core rlx8721d_rom_symbol_acut.ld; each
#                                  KM-image link stage cats the one for its core via
#                                  AMEBAD_CMSIS_ROM_KM{4,0}_LD (KM4 != KM0 addrs).

add_library(amebad_cmsis_rom INTERFACE)
target_include_directories(amebad_cmsis_rom SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")

set(AMEBAD_CMSIS_ROM_KM4_LD "${CMAKE_CURRENT_SOURCE_DIR}/cmsis_rom_km4.ld"
    CACHE INTERNAL "AmebaD KM4 (HP) CMSIS mpu/DSP ROM-symbol PROVIDE linker script")
set(AMEBAD_CMSIS_ROM_KM0_LD "${CMAKE_CURRENT_SOURCE_DIR}/cmsis_rom_km0.ld"
    CACHE INTERNAL "AmebaD KM0 (LP) CMSIS mpu ROM-symbol PROVIDE linker script")
