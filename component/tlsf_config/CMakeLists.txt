# tlsf_config — cross-platform memory manager over the upstream TLSF allocator, a
# SHARED component. kmalloc/kfree/krealloc over one or more TLSF heaps. The TLSF
# backend is the pristine mattconte/tlsf submodule (component/tlsf_config/tlsf);
# mm.c/mm_debug.c/tlsf_allocator.c are the project's manager + adaptation layer over
# it. Bouffalo links the real bflb_lhal/bflb_log; every other platform uses the
# portable shims in port_generic/ (IS_ENABLED + bflb_irq + cache-line + log, no SDK
# deps) and provides a STRONG mm_lock_save/mm_unlock_restore.
#
# TLSF (Two-Level Segregated Fit) is the only allocator backend. The submodule is kept
# unmodified: the one feature the old vendored copy added (an O(1) tlsf_free_size)
# is reimplemented in tlsf_allocator.c via the public tlsf_walk_pool API.

add_library(tlsf_config STATIC EXCLUDE_FROM_ALL
    mm.c
    mm_debug.c
    tlsf/tlsf.c
    tlsf_allocator.c
)

target_include_directories(tlsf_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Upstream tlsf.h (tlsf/) is PRIVATE: only tlsf_allocator.c #includes <tlsf.h>; no
# public header pulls it, so consumers don't need it on their path.
target_include_directories(tlsf_config PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tlsf"
)

# mm.h includes bflb_l1c.h; mm.c/mm_debug.c/tlsf_allocator.c include bflb_core.h +
# log.h. On Bouffalo those come from bflb_lhal/bflb_log; elsewhere the port_generic
# shims supply portable equivalents (kept off the PUBLIC path so they don't shadow
# the real SDK headers when this is built for Bouffalo).
if(BOUFFALO_CHIP)
    target_link_libraries(tlsf_config PUBLIC bflb_lhal bflb_log)
else()
    # PUBLIC: mm.h (a public header) #includes <bflb_l1c.h>, so consumers of tlsf_config
    # need the shims on their path too — not just tlsf_config's own sources.
    target_include_directories(tlsf_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/port_generic")
    target_compile_options(tlsf_config PRIVATE -w)   # upstream tlsf + Bouffalo mm on a new arch
endif()

# TLSF uses __builtin_clz/__builtin_ffs. armv8-m.base (AmebaD KM0) has no CLZ, so GCC
# emits libgcc soft-intrinsic calls (__clzsi2/__ffssi2), and the LP core links
# -nostdlib (no libgcc by default). Make libgcc a dependency OF tlsf_config so it
# follows tlsf_config in the link line (a direct -lgcc on the image would land before
# the transitively-pulled tlsf_config and miss the references). Only KM0 needs this;
# every other platform links libgcc as usual.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    target_link_libraries(tlsf_config PUBLIC gcc)
endif()
