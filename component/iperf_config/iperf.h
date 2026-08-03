/*
 * iperf.h — cross-platform iperf2-protocol throughput probes over FreeRTOS-Plus-TCP raw
 * sockets (component/iperf_config). Homegrown (static TX buffer, no iperf3 mmap/pthread/cJSON).
 * Each *_start() spawns a task. AmebaD auto-runs iperf_server_start() from its network-up hook;
 * bl602 exposes them via the `iperf` console command (iperf_cli_register).
 */
#ifndef IPERF_H
#define IPERF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void iperf_server_start( void );       /* TCP RX sink, device-as-server :5001 (host: iperf -c <ip>) */
void iperf_client_start( void );       /* TCP TX, device -> host (target from iperf_target_ip) */
void iperf_udp_server_start( void );   /* UDP RX sink :5001 */
void iperf_udp_client_start( void );   /* UDP TX, device -> host */

/* Target host for the TX (upload) probes, as a FreeRTOS+TCP network-order address.
 * Resolved from the IPERF_HOST env var (dotted quad) so the bench host can change
 * without a rebuild; falls back to the built-in default when unset/unparsable. */
uint32_t iperf_target_ip( void );

/* Register the `iperf [server|tx [ip]|udp|udptx [ip]]` console command. */
void iperf_cli_register( void );

#ifdef __cplusplus
}
#endif

#endif /* IPERF_H */
