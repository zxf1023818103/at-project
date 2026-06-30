# component/console — cross-platform stdio (fd 0/1/2) console dispatcher.
#
# The newlib retarget on each platform (AmebaD component/amebad/syscall_port,
# Bouffalo component/bouffalo_sdk/bflb_libc, ...) routes the three console fds
# here; per-platform code supplies the byte I/O via at_console_set_backend() or a
# strong at_console_default_backend() override. Owns the canonical line read
# (CR/LF -> '\n' + echo) and the stdout/stderr split once, shared by all.
#
# Header-only API + one TU. STATIC + EXCLUDE_FROM_ALL: built only when a
# platform's retarget links it (like component/lfs_config). Pure C, no platform
# deps -> usable by every platform build.
add_library(at_console STATIC EXCLUDE_FROM_ALL console.c)
target_include_directories(at_console PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
