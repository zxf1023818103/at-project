# amebad/monitor — vendored SDK debug-monitor / shell sources

The AmebaD SDK's debug monitor / shell console (`shell_init_ram`, the command
table, UART low-level I/O, trace), vendored out of
`platform/ameba-rtos-d/component/soc/realtek/amebad/app/monitor/` so the build no
longer compiles them in-place from the SDK tree.

- `ram/` — RAM-resident sources:
  - `monitor_lp.c` (KM0) / `monitor_hp.c` (KM4) — per-core monitor entry
  - `shell_ram.c`, `rtl_trace.c`, `low_level_io.c` — shared by both cores
- `rom/` — RAM wrappers around the on-chip ROM shell/monitor (`shell_init_rom`,
  …): `shell_rom.c`, `monitor_rom.c` (KM4 only).

One shared copy, compiled per-ABI: `monitor_km0` builds the KM0 subset, `monitor_km4`
builds the KM4 subset (mirrors the `freertos_glue/` + `freertos_glue_km{0,4}/`
split). The monitor *headers* (`app/monitor/include`, `swlib/string`, …) still
come from the SDK via `amebad_sdk_includes`. Resync from the SDK path above if
the `platform/ameba-rtos-d` submodule pointer moves.
