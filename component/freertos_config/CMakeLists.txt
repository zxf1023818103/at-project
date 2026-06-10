# component/freertos_config — the single, cross-platform FreeRTOSConfig.h.
#
# Header-only INTERFACE lib exposing ONE FreeRTOSConfig.h for the whole project,
# platform-selected by preprocessor macros (CONFIG_PLATFORM_8721D with KM0/KM4 via
# __ARM_ARCH_8M_BASE__/__ARM_ARCH_8M_MAIN__; BLxxx for Bouffalo). There are no
# per-platform FreeRTOSConfig.h copies anymore. Each platform's `freertos_config`
# (the kernel's mandated config target) links this so the include path reaches
# every TU that pulls FreeRTOSConfig.h.
add_library(freertos_config_common INTERFACE)
# SYSTEM: the kernel compiles with -Wpedantic -Werror; the Bouffalo configASSERT
# uses __FUNCTION__ (non-ISO). The old per-platform config was an -isystem header,
# which suppressed that; keep it a system include so the consolidated header is too.
target_include_directories(freertos_config_common SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
