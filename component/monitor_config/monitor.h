/*
 * monitor.h — the cross-platform interactive console (FreeRTOS-Plus-CLI engine).
 *
 * The portable engine (line editor + FreeRTOS_CLIProcessCommand loop + the generic
 * version/heap/tasks/echo commands) lives in component/monitor_config/monitor.c and
 * does all byte I/O through component/console (at_console_write_raw for un-timestamped
 * output; the platform RX via the seam below). Platform glue — the RX backend, any
 * dual-core console hand-off, reboot, and HW-specific commands — lives in each
 * platform's component/<platform>/monitor_port and is threaded in via the seam.
 *
 * Boot: <platform>/main.c calls monitor_start() once, then registers the command
 * modules it wants (fs_cli_register / env_cli_register / time_cli_register from the
 * shared monitor_cmds, plus any platform-specific *_cli_register()).
 */
#ifndef MONITOR_H
#define MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register the generic commands (version/heap/heapreset/tasks/echo) and spawn the
 * console task. Idempotent-unsafe: call exactly once. */
void monitor_start( void );

/* ===== Platform port seam — provided by component/<platform>/monitor_port ===== */

/* One-time port setup (RX backend install / console-ownership init). Called first,
 * inside monitor_start(), before the task is created. */
void monitor_port_init( void );

/* This core's identity for the prompt/banner ("KM4" / "KM0" / "BL602"). */
const char * monitor_core_name( void );

/* Fetch the next input byte with console-ownership semantics:
 *   > 0 : *c holds a byte (this core owns the console).
 *     0 : idle / timeout — the engine just re-loops (no line-state change).
 *   < 0 : ownership was just (re)gained — the engine resets its line buffer and
 *         reprints the banner + prompt. (Single-owner platforms return < 0 exactly
 *         once, on the first call, so the initial banner prints there.) */
int monitor_rx_get( uint8_t * c );

/* Called by the engine right after a command runs. Return 1 if a console hand-off
 * just occurred (the engine skips the prompt and loops to wait for hand-back); 0
 * otherwise. Single-owner platforms return 0. */
int monitor_after_command( void );

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_H */
