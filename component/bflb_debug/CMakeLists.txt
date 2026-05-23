# bflb_debug — extracted from platform/bouffalo_sdk/components/debug.
# DWARF CFI backtrace (unwind_6byte based) + optional MM leak tracking.

set(BFLB_DEBUG_SOURCES
    src/unwind_6byte.c
    src/backtrace.c
)

# Optional MM leak tracking — gated by SDK Kconfig flags. When building
# inside the SDK these come from auto_config; standalone build defaults off.
if(CONFIG_MM_ENABLE_TRACKING AND NOT CONFIG_MEM_HEAP5_EN)
    list(APPEND BFLB_DEBUG_SOURCES src/mm_track.c)
endif()

add_library(bflb_debug STATIC ${BFLB_DEBUG_SOURCES})

target_include_directories(bflb_debug PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_debug PRIVATE sdk_intf_lib)
endif()
