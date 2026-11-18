# component/bouffalo_sdk/syscall_port — Bouffalo's console backend for the
# cross-platform newlib retarget. The retarget core (the POSIX fd VFS + malloc +
# env + time + sockets) is the shared component/syscall_config; what stays here is
# the platform byte I/O: console_bflb_uart.c's strong at_console_default_backend()
# over bflb_uart, for the shared console dispatcher (component/console).
#
# The mounted-LittleFS hook the VFS needs (vfs_lfs_mount) is provided by the
# Bouffalo LittleFS port (component/bouffalo_sdk/lfs_port). The image
# links BOTH this OBJECT lib and the shared `syscall_config` OBJECT lib
# (force-included via $<TARGET_OBJECTS:> in bouffalo_sdk's umbrella so their strong
# defs beat libc's ENOSYS/weak-default stubs). Mirrors component/amebad/syscall_port.

# OBJECT lib (force-included like the core): console_bflb_uart.c's strong
# at_console_default_backend must override the shared console's weak default, which an
# archive member would not be pulled to do (no undefined ref). See component/syscall_config.
add_library(bouffalo_syscall_port OBJECT console_bflb_uart.c)

# bflb_uart.h (bflb_uart_putchar/getchar) from lhal; console.h (at_console_backend_t)
# from the shared dispatcher. at_console is PUBLIC so its archive propagates to the image.
target_link_libraries(bouffalo_syscall_port PRIVATE bflb_lhal)
target_link_libraries(bouffalo_syscall_port PUBLIC at_console)
# FreeRTOS StreamBuffer + FreeRTOSConfig.h for the IRQ-driven console RX (OBJECT lib: headers only).
target_link_libraries(bouffalo_syscall_port PRIVATE FreeRTOS)
