/* Extra ROM symbol PROVIDEs the WiFi driver (lib_wlan.a) needs in the KM4
 * image2 but which the stock app_km4 ROM symbol table (rlx8721d_rom_symbol_
 * acut.ld) does not list.
 *
 * PAD_CMD is a _LONG_CALL_ ROM pinmux helper; its ROM address is taken from the
 * KM4 bootloader's ROM symbol table (bootloader_km4/rlx8721d_rom_symbol_acut_
 * boot.ld: PAD_CMD = 0x10102f99) — same core, same ROM, so the address applies
 * to image2 too. PROVIDE so a compiled definition (if one is ever linked) wins.
 */
PROVIDE(PAD_CMD = 0x10102f99);
