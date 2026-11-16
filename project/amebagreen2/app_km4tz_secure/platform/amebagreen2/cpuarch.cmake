# Use the Armv8-M CMSE calling sequence supported by the vendor ROM/toolchain.
# The NS SDK can still use its Armv8.1-M instruction selection.
set(TFM_SYSTEM_PROCESSOR cortex-m33)
set(TFM_SYSTEM_ARCHITECTURE armv8-m.main)
set(TFM_SYSTEM_DSP OFF)
set(CONFIG_TFM_FP_ARCH fpv5-sp-d16)
set(CONFIG_TFM_FP_ARCH_ASM FPv5_SP_D16)
