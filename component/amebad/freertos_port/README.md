# amebad/freertos_port — AmebaD heap_5 memory-map glue

The `freertos_port` STATIC lib: a single vendored SDK FreeRTOS extension that is **not**
part of the upstream FreeRTOS kernel.

- `freertos_heap5_config.c` — the `heap_5` region table + `os_heap_init()` for the AmebaD
  memory map (only the `CONFIG_PLATFORM_8721D` branch compiles; the other-chip branches are
  kept verbatim for easy diffing against the SDK). Fills `vPortDefineHeapRegions` from the
  chip linker symbols.
- `freertos_service.h` / `freertos_heap5_config.h` — the glue headers the SDK os-abstraction
  pulls (exposed to consumers via `amebad_freertos_headers` in `component/amebad/sdk_headers`).

Copied verbatim from `platform/ameba-rtos-d/component/os/freertos/`. Resync from there if the
`platform/ameba-rtos-d` submodule pointer moves.

One target name on both cores: KM0 and KM4 configure into separate build trees with different
toolchains, so `freertos_port` never collides — no per-core suffix.

Everything else that used to live here moved to its proper owner:
- the upstream FreeRTOS kernel (`FreeRTOS`) + per-core port/heap + the backtrace/hooks
  extensions → `component/freertos-kernel_config`;
- the FreeRTOS header bundle (`amebad_freertos_headers`) + the
  `freertos_config <- amebad_sdk_includes` attach → `component/amebad/sdk_headers`.
