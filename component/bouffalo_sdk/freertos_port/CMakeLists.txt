# component/bouffalo_sdk/freertos_port — the Bouffalo add-ons on top of FreeRTOS: just the port
# glue (freertos_port.c — timer/IRQ setup) + atomic_rtos.h (Bouffalo's atomic helpers; upstream's
# analogue header atomic.h has different content).
#
# Everything else moved to its proper owner (tree structure):
#   - the upstream FreeRTOS kernel (-> `FreeRTOS`) + per-chip RISC-V port/heap is built by
#     component/freertos-kernel_config (Bouffalo's divergent kernel files are folded onto the
#     `bouffalo-port` branch of the zxf1023818103/FreeRTOS-Kernel.git fork it points at).
# One target name on all chips: bl602/bl616/bl616cl configure into separate build trees with
# different toolchains, so `freertos_port` never collides — no per-chip suffix needed.
add_library(freertos_port STATIC freertos_port.c)
target_include_directories(freertos_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(freertos_port FreeRTOS)

# Bouffalo-specific (no AmebaD analogue): attach the RISC-V FreeRTOS config onto the shared
# freertos_config (repo root, component/freertos-kernel_config) — done here, in this real
# component, not in the bouffalo_sdk umbrella. The RISC-V port asm needs the chip mtime /
# trap-handler defs at compile time, and freertos_config's extension sources (heap/hooks/backtrace)
# need FreeRTOSConfig.h (this dir) + the kernel headers. CMake resolves target usage-requirements at
# generate time, so even though the umbrella adds the kernel BEFORE this subdir, the kernel still
# compiles with these defs.
if("${BOUFFALO_CHIP}" STREQUAL "bl602")
    set(_mtime_base    "0x0200BFF8UL")
    set(_mtimecmp_base "0x02004000UL")
elseif("${BOUFFALO_CHIP}" STREQUAL "bl618dg")
    # B0 silicon is a Nuclei-900 core: its SysTimer is at 0x18030000, NOT the
    # T-Head CLINT at 0xE0000000 the other Bouffalo RISC-V chips use. With the
    # wrong base, vPortSetupTimerInterrupt takes a Load access fault (mtval
    # 0xe000bff8-ish) before the scheduler starts. Mirrors the SDK's
    # components/os/freertos/CMakeLists.txt bl618dg (non-a0) branch.
    set(_mtime_base    "0x18030000UL")
    set(_mtimecmp_base "0x18030008UL")
else()
    # bl616, bl616cl
    set(_mtime_base    "0xE000BFF8UL")
    set(_mtimecmp_base "0xE0004000UL")
endif()
target_include_directories(freertos_config SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"                   # atomic_rtos.h + FreeRTOSConfig.h reach
)
target_compile_definitions(freertos_config PUBLIC
    "configMTIME_BASE_ADDRESS=${_mtime_base}"
    "configMTIMECMP_BASE_ADDRESS=${_mtimecmp_base}"
    portasmHANDLE_INTERRUPT=interrupt_entry
    default_interrupt_handler=freertos_risc_v_trap_handler
)
# Kernel include dirs as directory strings (TARGET_PROPERTY, not a link) so no cycle forms
# (freertos_kernel_include links back to freertos_config for FreeRTOSConfig.h). Use
# freertos_kernel_port_headers (the INTERFACE headers target FreeRTOS-Kernel V11 added) instead of
# freertos_kernel_port: V11 made the latter an OBJECT library, and an OBJECT target inside the
# freertos_config<->freertos_kernel usage cycle makes CMake's generate step fail ("strongly
# connected component"). The INTERFACE headers target carries the same port include dirs without
# the OBJECT-in-a-cycle problem.
target_include_directories(freertos_config SYSTEM PRIVATE
    $<TARGET_PROPERTY:freertos_kernel_include,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:freertos_kernel_port_headers,INTERFACE_INCLUDE_DIRECTORIES>
)
