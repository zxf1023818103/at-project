# bflb_freertos_port — Bouffalo's add-ons on top of upstream FreeRTOS-Kernel.
#
# Upstream's divergent files were folded back into the kernel itself: the
# patches now live on the `bouffalo-port` branch of the
# https://github.com/zxf1023818103/FreeRTOS-Kernel.git fork, which is what
# component/thridparty/freertos-kernel points at. That leaves only the Bouffalo-only
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
# Chip-specific configMTIME_* / portasmHANDLE_INTERRUPT defines live on
# the parent umbrella's freertos_config INTERFACE target so the kernel
# itself sees them at compile time, not only consumers downstream.
target_link_libraries(bflb_freertos_port PUBLIC freertos_kernel)
