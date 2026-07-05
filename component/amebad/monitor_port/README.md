# amebad/monitor — the AmebaD console (FreeRTOS-Plus-CLI, both cores, KM0<->KM4 switch)

The interactive console for the RTL8721D. Replaces the legacy vendored AT-monitor shell (the ROM
shell machinery + the `.cmd.table.data` command table + the LOGUART RX IRQ). It is a
FreeRTOS-Plus-CLI task on **both** cores; input is **polled** (`LOGUART_Readable` /
`LOGUART_GetChar`) so the LOGUART RX IRQ is never enabled — which avoids the ROM
"Irq 3 Fun Not Assign" storm an enabled-but-unhandled RX line causes.

Sources:
- `monitor_cli.c` (`amebad_monitor_common`, both cores) — the engine (poll loop + line editing +
  the FreeRTOS+CLI process loop), the baseline commands (`version` / `heap` / `tasks` / `echo` /
  `reboot`), the console hand-off commands (`>>` toggle, `km0` / `km4` jump), the console-owner
  state, and the IPC channel-0 handler `shell_switch_ipc_int` + the entry `amebad_monitor_start()`.
- `monitor_plat_hp.c` (KM4) / `monitor_plat_lp.c` (KM0) (`amebad_monitor`) — the only per-core
  seam: `amebad_monitor_core_name()` ("KM4" / "KM0").
- `monitor.h` — the public API (`amebad_monitor_start`, `amebad_monitor_core_name`).

## Console hand-off (the `>>` / `km0` / `km4` switch)
The two cores share one LOGUART. Since the CLI polls (no RX IRQ to hand off), it hands off a
logical **console owner** token: only the owning core's task reads the UART; the other core's task
waits. KM4 owns at boot; a switch command on the owner sets its `g_owner=0` and pings IPC channel 0
(`IPC_INT_CHAN_SHELL_SWITCH`), whose handler on the other core sets *its* `g_owner=1` — that core's
task picks it up within one 50 ms poll and prints its prompt. `>>` always toggles; `km0`/`km4` jump
to a named core (no-op if already there).

## Retained SDK header/ROM surface (not the shell)
`shell.h`, `monitor_rom.h`, `rtl_trace.h` stay — they are SDK ABI headers the umbrella `rtl8721d.h`
/ `string_rom/diag.h` pull (types + ROM decls), independent of the deleted shell `.c`. `monitor_rom.ld`
stays too: the KM0 **bootloader** still calls the ROM boot-shell `shell_rom` (boot-time uartburn
console) — its PROVIDE table is cat'd into the KM0 bootloader link (`AMEBAD_MONITOR_ROM_LD`), no
longer into image2.

The console links `freertos_cli` (FreeRTOS-Plus-CLI core, `component/freertos-cli_config`). On KM0
(no libc under `-nostdlib`) the CLI's `str*`/`*printf` deps are aliased to ROM swlib routines in
`app_km0/rlx8721d_img2_romalias.ld`.
