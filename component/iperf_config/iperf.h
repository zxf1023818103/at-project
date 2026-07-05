/*
 * iperf.h — cross-platform iperf2-protocol throughput probes over FreeRTOS-Plus-TCP raw
 * sockets (component/iperf_config). Homegrown (static TX buffer, no iperf3 mmap/pthread/cJSON).
 * Each *_start() spawns a task. AmebaD auto-runs iperf_server_start() from its network-up hook;
 * bl602 exposes them via the `iperf` console command (iperf_cli_register).
 */
#ifndef IPERF_H
#define IPERF_H

#ifdef __cplusplus
extern "C" {
#endif

void iperf_server_start( void );       /* TCP RX sink, device-as-server :5001 (host: iperf -c <ip>) */
void iperf_client_start( void );       /* TCP TX, device -> host (built-in target IP) */
void iperf_udp_server_start( void );   /* UDP RX sink :5001 */
void iperf_udp_client_start( void );   /* UDP TX, device -> host */

/* Register the `iperf [server|tx|udp|udptx]` console command. */
void iperf_cli_register( void );

#ifdef __cplusplus
}
#endif

#endif /* IPERF_H */
