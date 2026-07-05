# component/bouffalo_sdk/monitor_port — the Bouffalo (BL602/BL616/BL616CL) port for the shared
# console engine (component/monitor_config, target `monitor`). Provides the monitor.h seam
# (monitor_port_init / monitor_core_name / monitor_rx_get / monitor_after_command) as a simple
# single-core poll over at_console, plus a `reboot` command. Target `monitor_port` (mirrors the
# AmebaD port target name). bl_sys_reset_por resolves at the image link (bflb_sys, in the umbrella
# group), so nothing platform-heavy is linked here.
add_library(monitor_port STATIC monitor_bflb.c)

# The shared engine brings FreeRTOS + freertos_cli + at_console (and monitor.h) transitively.
target_link_libraries(monitor_port PUBLIC monitor)

# The prompt/banner identity string, per chip (bl602 / bl616 / bl616cl).
target_compile_definitions(monitor_port PRIVATE MONITOR_CORE_NAME="${BOUFFALO_CHIP}")
