# bflb_freertos_port — Bouffalo's add-ons on top of upstream FreeRTOS-Kernel.
#
# Upstream's divergent files were folded back into the kernel itself: the
# patches now live on the `bouffalo-port` branch of the
# https://github.com/zxf1023818103/FreeRTOS-Kernel.git fork, which is what
# component/freertos-kernel_config/freertos-kernel points at. That leaves only the Bouffalo-only
# additions here:
#
#   freertos_port.c           Bouffalo's port glue (timer/IRQ setup, etc.)
#   include/atomic_rtos.h     Bouffalo's atomic helpers (upstream's analogue
#                             header is atomic.h with different content).

add_library(bflb_freertos_port STATIC
    freertos_port.c
)

target_include_directories(bflb_freertos_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Anything that links bflb_freertos_port pulls in the FreeRTOS kernel
# (including the Bouffalo patches now on the bouffalo-port branch).
target_link_libraries(bflb_freertos_port PUBLIC freertos_kernel)

# Attach the Bouffalo RISC-V FreeRTOS config onto the shared freertos_config (repo
# root, component/freertos-kernel_config) — done here, in this real component, not in the
# bouffalo_sdk umbrella. The RISC-V port asm needs the chip mtime / trap-handler defs
# at compile time, and freertos_config's extension sources (heap/hooks/backtrace) need
# FreeRTOSConfig.h (this dir's include/) + the kernel headers. CMake resolves target
# usage-requirements at generate time, so even though the umbrella adds the kernel
# BEFORE this subdir, the kernel still compiles with these defs.
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    set(_mtime_base    "0x0200BFF8UL")
    set(_mtimecmp_base "0x02004000UL")
else()
    # bl616, bl616cl
    set(_mtime_base    "0xE000BFF8UL")
    set(_mtimecmp_base "0xE0004000UL")
endif()
target_include_directories(freertos_config SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"          # atomic_rtos.h + FreeRTOSConfig.h reach
)
target_compile_definitions(freertos_config PUBLIC
    "configMTIME_BASE_ADDRESS=${_mtime_base}"
    "configMTIMECMP_BASE_ADDRESS=${_mtimecmp_base}"
    portasmHANDLE_INTERRUPT=interrupt_entry
    default_interrupt_handler=freertos_risc_v_trap_handler
)
# Kernel include dirs as directory strings (TARGET_PROPERTY, not a link) so no cycle
# forms (freertos_kernel_include links back to freertos_config for FreeRTOSConfig.h).
# Use freertos_kernel_port_headers (the INTERFACE headers target FreeRTOS-Kernel V11
# added) instead of freertos_kernel_port: V11 made the latter an OBJECT library, and an
# OBJECT target inside the freertos_config<->freertos_kernel usage cycle makes CMake's
# generate step fail ("strongly connected component"). The INTERFACE headers target
# carries the same port include dirs without the OBJECT-in-a-cycle problem.
target_include_directories(freertos_config SYSTEM PRIVATE
    $<TARGET_PROPERTY:freertos_kernel_include,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:freertos_kernel_port_headers,INTERFACE_INCLUDE_DIRECTORIES>
)
