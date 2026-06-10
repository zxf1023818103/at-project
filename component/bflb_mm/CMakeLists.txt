# bflb_mm — cross-platform memory manager (TLSF / heap5 wrappers), now a SHARED
# component (moved out of component/bouffalo_sdk). kmalloc/kfree/krealloc over one
# or more heaps. Bouffalo links the real bflb_lhal/bflb_log; every other platform
# uses the portable shims in port_generic/ (IS_ENABLED + bflb_irq + cache-line +
# log, no SDK deps) and provides a STRONG mm_lock_save/mm_unlock_restore.
#
# Allocator selection mirrors the SDK:
#   CONFIG_MEM_TLSF_EN   (default ON) -> TLSF allocator
#   CONFIG_MEM_HEAP5_EN  (default OFF) -> FreeRTOS heap_5 allocator

if(NOT DEFINED CONFIG_MEM_TLSF_EN)
    set(CONFIG_MEM_TLSF_EN ON)
endif()

add_library(bflb_mm STATIC EXCLUDE_FROM_ALL
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

# mm.h includes bflb_l1c.h; mm.c/mm_debug.c/tlsf_allocator.c include bflb_core.h +
# log.h. On Bouffalo those come from bflb_lhal/bflb_log; elsewhere the port_generic
# shims supply portable equivalents (kept off the PUBLIC path so they don't shadow
# the real SDK headers when this is built for Bouffalo).
if(BOUFFALO_CHIP)
    target_link_libraries(bflb_mm PUBLIC bflb_lhal bflb_log)
else()
    # PUBLIC: mm.h (a public header) #includes <bflb_l1c.h>, so consumers of bflb_mm
    # need the shims on their path too — not just bflb_mm's own sources.
    target_include_directories(bflb_mm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/port_generic")
    target_compile_options(bflb_mm PRIVATE -w)   # vendored sources on a new arch
endif()
