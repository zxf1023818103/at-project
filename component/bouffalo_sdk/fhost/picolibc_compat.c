/*
 * picolibc -> newlib shim for the prebuilt bl618dg WiFi archives.
 *
 * Bouffalo builds BL618DG's fhost / wpa_supplicant archives with the Zephyr SDK,
 * whose C library is picolibc. There, assert() expands to a call to a no-argument
 * helper:
 *
 *     #define assert(__e) ((__e) ? (void)0 : __assert_no_args())
 *
 * This project links the Nuclei toolchain's newlib (see
 * cmake/riscv64-nuclei-elf.cmake), which has no such symbol, so those archives
 * fail to link with "undefined reference to `__assert_no_args'".
 *
 * Provide it and route it to newlib's __assert_func, so an assertion tripped
 * inside the prebuilt WiFi stack still reports through the normal path instead of
 * vanishing. The file/line the vendor code passed is lost -- picolibc's variant
 * takes no arguments by design (it trades the message for code size) -- so the
 * best we can name is the archive it came from.
 */

#include <assert.h>
#include <stddef.h>

__attribute__( ( noreturn ) ) void __assert_no_args( void );

__attribute__( ( noreturn ) ) void __assert_no_args( void )
{
    __assert_func( "<prebuilt bl618dg wifi archive>", 0, NULL,
                   "assertion failed (picolibc-style, no location available)" );
    __builtin_unreachable();
}
