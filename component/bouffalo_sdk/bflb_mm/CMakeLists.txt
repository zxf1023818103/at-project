# bflb_mm — Bouffalo Memory Management Framework (TLSF / heap5 wrappers).
# Extracted from bouffalo_sdk/components/mm.
#
# Allocator selection mirrors the SDK:
#   CONFIG_MEM_TLSF_EN   (default ON) -> TLSF allocator
#   CONFIG_MEM_HEAP5_EN  (default OFF) -> FreeRTOS heap_5 allocator

if(NOT DEFINED CONFIG_MEM_TLSF_EN)
    set(CONFIG_MEM_TLSF_EN ON)
endif()

add_library(bflb_mm STATIC
    mm.c
    mm_debug.c
)

target_include_directories(bflb_mm PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CONFIG_MEM_TLSF_EN)
    target_sources(bflb_mm PRIVATE
        tlsf/tlsf.c
        tlsf/tlsf_allocator.c
    )
    target_include_directories(bflb_mm PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/tlsf"
    )
endif()

if(CONFIG_MEM_HEAP5_EN)
    target_sources(bflb_mm PRIVATE
        heap5/heap5.c
        heap5/heap5_allocator.c
    )
    target_include_directories(bflb_mm PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/heap5"
    )
endif()

# mm.h includes bflb_l1c.h (lhal); mm.c / mm_debug.c include bflb_core.h (lhal)
# and log.h (bflb_log).
target_link_libraries(bflb_mm PUBLIC bflb_lhal bflb_log)
