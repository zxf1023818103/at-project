# mklfs

Host-side tool for building [LittleFS] disk images from a directory of
files. Used by the project's build to pack `project/resource/` into
`project/lfs.bin` at configure-time, then embedded at flash offset
`0x300000` on every supported chip (BL602 / BL616 / BL616CL / Ameba D /
Ameba D+ / Ameba Green 2).

## Source

`mklfs.c` is an unmodified vendored copy from the **Lua-RTOS-ESP32**
project — the same `mklfs` linked from upstream littlefs's README as
the recommended image-builder for host PCs.

- Upstream URL: <https://github.com/whitecatboard/Lua-RTOS-ESP32/tree/master/components/mklfs/src>
- Copyright: IBEROXARXA SERVICIOS INTEGRALES, S.L. / Jaume Olivé Petrus
  (whitecatboard.org) — see the file header
- Vendored here rather than pulled as a git submodule because the
  upstream lives inside a much larger ESP32-stack repo and the source
  is a single file that has not changed materially in years.

This file was previously also vendored as
`platform/bouffalo_sdk/tools/genlfs/mklfs.c` inside the Bouffalo SDK
submodule (a byte-identical copy with the same upstream header). The
project now uses this copy under `component/` so the build no longer
depends on a path inside the SDK submodule.

## Build

The host tool is built via `cmake/lfs_image.cmake` + the host
sub-project under `cmake/lfs_image/CMakeLists.txt`, which compiles
`mklfs.c` together with upstream littlefs's `lfs.c` / `lfs_util.c` from
`component/littlefs/` using whatever host C compiler is on `PATH`
(`cc` / `gcc` / `clang`). The outer cross-compile build (RISC-V
toolchain) launches the host build through `ExternalProject_Add` so
target + host compilers stay in separate CMake caches.
