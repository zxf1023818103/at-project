# component/amebad/freertos_port — the AmebaD memory-map glue for FreeRTOS: just the heap_5
# region table (freertos_heap5_config.c fills vPortDefineHeapRegions from the chip linker symbols).
#
# Everything else moved to its proper owner (tree structure):
#   - the upstream FreeRTOS kernel (-> `FreeRTOS`) + per-core port/heap is built by
#     component/freertos-kernel_config (it derives the port from AMEBAD_CHIP).
#   - the FreeRTOS kernel/glue HEADER bundle (amebad_freertos_headers) + the
#     freertos_config <- amebad_sdk_includes attach (platform_autoconf.h) live in
#     component/amebad/sdk_headers, the owner of the AmebaD SDK include bundle.
# One target name on both cores: KM0 and KM4 configure into separate build trees with different
# toolchains, so `freertos_port` never collides — no per-core suffix needed.
add_library(freertos_port STATIC freertos_heap5_config.c)
target_include_directories(freertos_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(freertos_port FreeRTOS)
