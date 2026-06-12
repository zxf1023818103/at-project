# bflb_mm — cross-platform memory manager (TLSF allocator), now a SHARED component
# (moved out of component/bouffalo_sdk). kmalloc/kfree/krealloc over one or more
# TLSF heaps. Bouffalo links the real bflb_lhal/bflb_log; every other platform uses
# the portable shims in port_generic/ (IS_ENABLED + bflb_irq + cache-line + log, no
# SDK deps) and provides a STRONG mm_lock_save/mm_unlock_restore.
#
# TLSF (Two-Level Segregated Fit) is the only allocator backend. (The optional
# FreeRTOS heap_5 backend that mirrored the SDK was removed — unused everywhere.)

add_library(bflb_mm STATIC EXCLUDE_FROM_ALL
    mm.c
    mm_debug.c
    tlsf.c
    tlsf_allocator.c
)

target_include_directories(bflb_mm PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

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

# TLSF uses __builtin_clz/__builtin_ffs. armv8-m.base (AmebaD KM0) has no CLZ, so GCC
# emits libgcc soft-intrinsic calls (__clzsi2/__ffssi2), and the LP core links
# -nostdlib (no libgcc by default). Make libgcc a dependency OF bflb_mm so it follows
# bflb_mm in the link line (a direct -lgcc on the image would land before the
# transitively-pulled bflb_mm and miss the references). Only KM0 needs this; every
# other platform links libgcc as usual.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    target_link_libraries(bflb_mm PUBLIC gcc)
endif()
