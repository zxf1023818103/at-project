# component/freertos-config — cross-platform FreeRTOS config base.
#
# Header-only INTERFACE lib exposing freertos_config_common.h: the FreeRTOS
# settings identical across AmebaD KM0/KM4 (Cortex-M) and Bouffalo (RISC-V). Each
# platform's FreeRTOSConfig.h #includes it for the shared feature set and keeps
# only its hardware/port specifics. Each platform's freertos_config (the kernel's
# mandated config target) links this so the include path reaches every TU that
# pulls FreeRTOSConfig.h.
add_library(freertos_config_common INTERFACE)
target_include_directories(freertos_config_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
