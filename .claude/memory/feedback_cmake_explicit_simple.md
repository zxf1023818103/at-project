---
name: cmake-explicit-simple-sources
description: "User prefers plain explicit CMake — list each source in add_library, no file(GLOB) or clever foreach/list patterns"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 2c7314f2-7f7e-46e5-84f0-75d717be0da0
---

In this repo's CMakeLists, the user wants **simple, explicit, obvious** code. Two clarified rules (their words: "不要这样花里胡哨的，add_library 直接写一个个源文件就行了，简单易懂" then "重复的编译选项弄成变量是可以的，重复的源文件弄成library"):

1. **Source files: list explicitly** in `add_library(<tgt> STATIC a.c b.c ...)`, one per line, bare relative paths. **No `file(GLOB ...)`** (even with a tidy filename-prefix pattern). If source files are *shared* across targets, **extract them into a library** the others link (e.g. `amebad_monitor_common`) — don't repeat the file list.
2. **Repeated compile options → a variable is fine/preferred**: `set(_xxx_warnings -Wno-...)` then `target_compile_options(<tgt> PRIVATE ${_xxx_warnings})` per target. But **NOT** a `set(_targets ...)` list + `foreach(_t ...)` — call `target_compile_options` explicitly per target; the variable removes the repetition without hiding which targets exist.

**Why:** readability / "一眼能看懂" beats terseness; but factoring a repeated option *list* into a named var is still readable, so that's wanted.

**How to apply:** gate on `AMEBAD_CHIP`; per library a plain `add_library` + `target_link_libraries` + `target_compile_options(... ${_warnings})`. See `component/amebad/usrcfg/CMakeLists.txt` and `monitor/CMakeLists.txt` for the shape. Relates to [[amebad-sdk-decoupling-status]].
