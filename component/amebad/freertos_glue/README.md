# amebad/freertos_glue — vendored SDK FreeRTOS extension sources

Two AmebaD SDK FreeRTOS extensions that are **not** part of the upstream
FreeRTOS kernel, vendored out of the SDK so the driver-only build no longer
reaches into `platform/ameba-rtos-d/component/os/freertos/`:

- `freertos_backtrace_ext.c` — crash/backtrace helpers that introspect the TCB
  (`vTaskStackAddr` / `vTaskStackTOPAddr` / `vTaskStackSize` / `vTaskName`).
- `freertos_heap5_config.c` — the `heap_5` region table + `os_heap_init()` for
  the AmebaD memory map (only the `CONFIG_PLATFORM_8721D` branch compiles; the
  other-chip branches are kept verbatim for easy diffing against the SDK).

Copied verbatim from
`platform/ameba-rtos-d/component/os/freertos/{freertos_backtrace_ext.c,freertos_heap5_config.c}`.
Resync from there if the `platform/ameba-rtos-d` submodule pointer moves.

The sources are chip-agnostic but ABI-specific: this one shared copy is
compiled twice — once under the KM0 toolchain (`amebad_freertos_glue_km0`) and
once under KM4 (`amebad_freertos_glue_km4`) — mirroring `common_ld_script/`.
This folder holds sources only; the build lives in the two
`freertos_glue_km{0,4}/CMakeLists.txt` that reference these files.
