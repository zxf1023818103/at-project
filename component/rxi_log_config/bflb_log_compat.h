/*
 * bflb_log_compat.h — Bouffalo SDK logging (bflb_log's LOG_* / BFLB_LOG_* macros) mapped onto the
 * cross-platform rxi logger (component/rxi_log_config). Lets the Bouffalo-specific bflb_log
 * component be deleted so the WHOLE project logs through one logger (rxi -> at_console). The few
 * SDK files that used bflb's "log.h" (rfparam_adapter) include this instead.
 *
 * bflb's LOG_I(fmt, ...) auto-prepended the file's DBG_TAG; rxi's log_info(fmt, ...) does not, so
 * the tag is dropped (the call-site messages carry their own context). BFLB_LOG_DEFINE_TAG and
 * friends become no-ops (the semicolon at the call site completes a harmless extern decl).
 */
#ifndef BFLB_LOG_COMPAT_H
#define BFLB_LOG_COMPAT_H

#include "at_log.h"   /* rxi log_trace/debug/info/warn/error/fatal + log_lock_init */

#ifndef DBG_TAG
#define DBG_TAG ""
#endif

#define BFLB_LOG_TAG                     DBG_TAG
#define BFLB_LOG_DEFINE_TAG(name, s, en) extern int bflb_log_tag_##name
#define BFLB_LOG_EXTERN_TAG(name)        extern int bflb_log_tag_##name
#define BFLB_LOG_GET_TAG(name)           ((void *)0)

#define LOG_F(...)   log_fatal(__VA_ARGS__)
#define LOG_E(...)   log_error(__VA_ARGS__)
#define LOG_W(...)   log_warn(__VA_ARGS__)
#define LOG_I(...)   log_info(__VA_ARGS__)
#define LOG_D(...)   log_debug(__VA_ARGS__)
#define LOG_T(...)   log_trace(__VA_ARGS__)
#define LOG_RF(...)  log_fatal(__VA_ARGS__)
#define LOG_RE(...)  log_error(__VA_ARGS__)
#define LOG_RW(...)  log_warn(__VA_ARGS__)
#define LOG_RI(...)  log_info(__VA_ARGS__)
#define LOG_RD(...)  log_debug(__VA_ARGS__)
#define LOG_RT(...)  log_trace(__VA_ARGS__)
#define LOG_FLUSH()  ((void)0)

#endif /* BFLB_LOG_COMPAT_H */
