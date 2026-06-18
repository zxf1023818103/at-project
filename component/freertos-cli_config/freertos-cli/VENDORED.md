# FreeRTOS-Plus-CLI — vendored copy

FreeRTOS-Plus-CLI has no standalone upstream repository; the source lives only inside
the FreeRTOS/FreeRTOS monorepo. These files are copied verbatim (no local edits) so the
component does not have to carry the whole monorepo as a submodule.

- Upstream repo:   https://github.com/FreeRTOS/FreeRTOS
- Upstream path:   FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI/
- Upstream commit: f60e36e45d0d7ea84febc8d1ea5f4e70f68cb3ec (main)
- Version:         FreeRTOS V202212.00
- License:         MIT (header in each source file)

Vendored files: `FreeRTOS_CLI.c`, `FreeRTOS_CLI.h`, `readme.txt`.

## To update

```sh
base="https://raw.githubusercontent.com/FreeRTOS/FreeRTOS/main/FreeRTOS-Plus/Source/FreeRTOS-Plus-CLI"
for f in FreeRTOS_CLI.c FreeRTOS_CLI.h readme.txt; do curl -fsSL "$base/$f" -o "$f"; done
```

Then bump the commit hash above. The wrapper build lives in `../CMakeLists.txt`.
