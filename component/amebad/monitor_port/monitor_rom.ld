/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM0 mask-ROM monitor / shell
 * command line: the shell_* and cmd_* ROM functions plus the shell_ctl/buf/
 * argv_array state. Moved out of common_ld_script/rlx8721d_rom_symbol_acut.ld so
 * the ROM shell symbols live with the monitor component. KM4 builds the shell
 * from source (monitor/shell_rom.c, monitor_rom.c), so there is no KM4 table.
 * Cat'd into every KM0-image combined linker script via AMEBAD_MONITOR_ROM_LD.
 * PROVIDE so unreferenced entries cost nothing and a real definition wins.
 */
PROVIDE(cmd_write_word = 0x8d9);
PROVIDE(cmd_dump_word = 0x91d);
PROVIDE(cmd_flash = 0x9f9);
PROVIDE(cmd_efuse = 0xf91);
PROVIDE(cmd_rom_table = 0x12e9);
PROVIDE(shell_array_init = 0x572d);
PROVIDE(shell_get_argc = 0x573b);
PROVIDE(shell_get_argv = 0x5767);
PROVIDE(shell_cmd_chk = 0x5831);
PROVIDE(shell_uart_irq_rom = 0x58f5);
PROVIDE(shell_init_rom = 0x59ad);
PROVIDE(shell_task_rom = 0x5a1d);
PROVIDE(shell_rom = 0x5a45);
PROVIDE(shell_argv_array = 0x80568);
PROVIDE(shell_ctl = 0x805a8);
PROVIDE(shell_recv_all_data_onetime = 0x805cc);
PROVIDE(shell_buf = 0x805d0);
