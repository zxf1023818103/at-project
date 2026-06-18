/*
 * monitor.h — public API of the AmebaD FreeRTOS-Plus-CLI console (component/amebad/monitor).
 *
 * Replaces the legacy AT-monitor shell (shell_init_ram/shell_init_rom). The console runs on
 * BOTH cores as a FreeRTOS-Plus-CLI task that POLLS the LOGUART (never enables the RX IRQ —
 * avoids the ROM "Irq 3 Fun Not Assign" storm). Only the core that currently OWNS the console
 * reads the shared LOGUART; `>>` / `km0` / `km4` hand ownership to the other core over IPC
 * channel 0 (IPC_INT_CHAN_SHELL_SWITCH). The channel-0 handler is shell_switch_ipc_int(),
 * still declared in the SDK's shell.h (so usrcfg/rtl8721d_ipccfg.c stays untouched) and
 * defined in monitor_cli.c.
 */
#ifndef AMEBAD_MONITOR_H
#define AMEBAD_MONITOR_H

/* Register the CLI commands and spawn the console task. Called from each core's main.c
 * (KM4 + KM0) in place of the old shell_init_ram()/shell_init_rom(). KM4 boots owning the
 * console; KM0 boots waiting for a hand-off. */
void amebad_monitor_start(void);

/* Per-core identity, provided by monitor_plat_hp.c (KM4) / monitor_plat_lp.c (KM0).
 * Returns the literal "KM4" or "KM0" — used for the prompt label and the switch logic. */
const char *amebad_monitor_core_name(void);

#endif /* AMEBAD_MONITOR_H */
