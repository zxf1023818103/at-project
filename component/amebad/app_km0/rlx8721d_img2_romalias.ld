/* CMAKE_PATCH: image2-only ROM libc aliases.
 *
 * This ameba-rtos-d checkout is missing swlib/std_lib/libc/rom/string/ — the
 * SDK header tree that redirects plain memcpy/memset to the ROM _memcpy/_memset
 * for code it doesn't own. SDK HAL calls _memcpy directly (resolved via the ROM
 * symbol table), but the FreeRTOS kernel (tasks.c / queue.c) calls plain
 * memcpy / memset, which would otherwise be undefined under -nostdlib.
 *
 * Alias them to the ROM symbols (0x6591 / 0x66ad, defined in
 * rlx8721d_rom_symbol_acut.ld). This fragment is cat'd AFTER the ROM symbol
 * table so _memcpy / _memset are already defined when these PROVIDEs evaluate.
 * Same end result as the SDK's compile-time redirect: both land on the ROM
 * routine. Scoped to image2 — the bootloaders don't reference memcpy/memset, so
 * this stays out of their link entirely.
 */
PROVIDE(memcpy = _memcpy);
PROVIDE(memset = _memset);
/* bflb_mm TLSF (now the shared heap on KM0 too) calls strncpy + printf, which the
 * LP core's -nostdlib link has no libc for. Alias to the ROM routines: _strncpy =
 * 0x7019 (string_rom_km0.ld); DiagPrintfD = 0x4b9, a printf-style logger to LOGUART.
 * (The libgcc intrinsics TLSF also needs — __clzsi2/__ffssi2, since armv8-m.base has
 * no CLZ — come from libgcc, which component/bflb_mm links on KM0.) */
PROVIDE(strncpy = _strncpy);
PROVIDE(printf  = DiagPrintfD);
