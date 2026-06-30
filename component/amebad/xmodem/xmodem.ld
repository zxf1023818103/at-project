/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM0 mask-ROM xmodem image-
 * download API (xmodem_uart_* / xmodem_img_* + the xmodem_ctrl / xmodem_uartx
 * state). Moved out of common_ld_script/rlx8721d_rom_symbol_acut.ld so the ROM
 * xmodem support lives with the xmodem component (xmodem_*.h).
 *
 * Cat'd into every KM0-image combined linker script (bootloader_km0,
 * bootloader_km0_2nd, app_km0) via AMEBAD_XMODEM_ROM_SYMBOL_LD. PROVIDE so a
 * real compiled definition (if ever linked) wins and unreferenced entries cost
 * nothing; references resolve to the KM0 mask ROM.
 */
PROVIDE(xmodem_img_rxbuffer = 0x5ae9);
PROVIDE(xmodem_uart_init = 0x60f5);
PROVIDE(xmodem_uart_deinit = 0x618d);
PROVIDE(xmodem_uart_port_init = 0x61a1);
PROVIDE(xmodem_uart_port_deinit = 0x61d5);
PROVIDE(xmodem_uart_readable = 0x6221);
PROVIDE(xmodem_uart_writable = 0x6239);
PROVIDE(xmodem_uart_getc = 0x6251);
PROVIDE(xmodem_uart_putc = 0x6275);
PROVIDE(xmodem_uart_putdata = 0x628d);
PROVIDE(xmodem_uart_getc_to = 0x62a9);
PROVIDE(xmodem_uart_clean_rx = 0x62f1);
PROVIDE(xmodem_img_write = 0x6331);
PROVIDE(xmodem_img_download = 0x634d);
PROVIDE(xmodem_ctrl = 0x80654);
PROVIDE(xmodem_uartx = 0x80660);
