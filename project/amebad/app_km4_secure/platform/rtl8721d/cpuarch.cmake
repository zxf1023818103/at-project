#-------------------------------------------------------------------------------
# RTL8721D (Ameba-D) KM4 — Real-M300, Armv8-M Mainline (Cortex-M33 compatible).
#
# CPU/arch flags mirror cmake/amebad-km4.cmake (the NS image build):
#   -march=armv8-m.main+dsp  -mfpu=fpv5-sp-d16  -mfloat-abi=hard
# i.e. DSP extension ON, single-precision FPU (FPv5-SP-D16) — NOT the
# double-precision fpv5-d16 the Arm MPS2/AN521 reference uses.
#-------------------------------------------------------------------------------

set(TFM_SYSTEM_PROCESSOR    cortex-m33)
set(TFM_SYSTEM_ARCHITECTURE armv8-m.main)
set(TFM_SYSTEM_DSP          ON)

# Single-precision FPU (KM4 = fpv5-sp-d16).
set(CONFIG_TFM_FP_ARCH      "fpv5-sp-d16")
set(CONFIG_TFM_FP_ARCH_ASM  "FPv5_SP_D16")
