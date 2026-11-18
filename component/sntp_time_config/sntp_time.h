/*
 * sntp_time.h — cross-platform coreSNTP UTC time sync (component/sntp_time_config).
 */
#ifndef SNTP_TIME_H
#define SNTP_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start the async SNTP client (idempotent; the task self-retries with backoff until the
 * network is up, then re-syncs hourly). Call once — e.g. after the console starts or from a
 * network-up hook. Feeds newlib's wall clock via amebad_set_unix_time (syscall_config). */
void sntp_sync_start( void );

/* Force one synchronous SNTP round now (the `sntp_update` console command). Returns 0 on
 * success. This is the strong definition that overrides time_cli.c's weak sntp_sync_once stub. */
int sntp_sync_once( void );

#ifdef __cplusplus
}
#endif

#endif /* SNTP_TIME_H */
