# amebad/monitor — vendored SDK debug-monitor / shell sources

The AmebaD SDK's debug monitor / shell console (`shell_init_ram`, the command
table, UART low-level I/O, trace), vendored out of
`platform/ameba-rtos-d/component/soc/realtek/amebad/app/monitor/` (its `ram/` and
`rom/` subdirs, flattened here) so the build no longer compiles them in-place
from the SDK tree.

- `monitor_lp.c` (KM0) / `monitor_hp.c` (KM4) — per-core monitor entry
- `shell_ram.c`, `rtl_trace.c`, `low_level_io.c` — shared by both cores
- `shell_rom.c`, `monitor_rom.c` — RAM wrappers around the on-chip ROM
  shell/monitor (`shell_init_rom`, …), KM4 only

Built per-ABI (see CMakeLists.txt): `amebad_monitor_common` holds the shared
sources; `amebad_monitor` holds the per-core entry (monitor_lp on KM0,
monitor_hp + the rom wrappers on KM4) and links the common lib. The monitor
*headers* (`app/monitor/include`, `swlib/string`, …) still come from the SDK via
`amebad_sdk_includes`. Resync from the SDK path above if the
`platform/ameba-rtos-d` submodule pointer moves.
