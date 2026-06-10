#ifndef BFLB_MM_PORT_LOG_H
#define BFLB_MM_PORT_LOG_H
/*
 * Portable shim of the Bouffalo SDK's <log.h> for non-Bouffalo builds of the
 * shared bflb_mm. The allocator only logs on error/diagnostic paths; keep it free
 * of printf/format dependencies (callers check return values). No-op everything.
 */
#define LOG_E(...) ((void)0)
#define LOG_W(...) ((void)0)
#define LOG_I(...) ((void)0)
#define LOG_D(...) ((void)0)
#define LOG_L(...) ((void)0)
#define LOG_R(...) ((void)0)

#endif /* BFLB_MM_PORT_LOG_H */
