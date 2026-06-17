#ifndef TLSF_MM_LOG_STUB_H
#define TLSF_MM_LOG_STUB_H
/*
 * No-op logging shims for non-Bouffalo builds of tlsf_config. The allocator only logs on
 * error/diagnostic paths (callers check return values), so keep it printf/format-free. Bouffalo
 * uses bflb_log's real <log.h> instead — see the MM_USE_BFLB_LOG guard in mm_debug.c /
 * tlsf_allocator.c. Named uniquely (NOT "log.h") so it can't shadow other components' log.h
 * (e.g. rxi log_config's) on the shared include path.
 */
#define LOG_E(...) ((void)0)
#define LOG_W(...) ((void)0)
#define LOG_I(...) ((void)0)
#define LOG_D(...) ((void)0)
#define LOG_L(...) ((void)0)
#define LOG_R(...) ((void)0)

#endif /* TLSF_MM_LOG_STUB_H */
