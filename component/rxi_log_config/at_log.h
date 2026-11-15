#ifndef AT_LOG_H
#define AT_LOG_H

/*
 * at-project logging — rxi's log.c (component/rxi_log_config/log) + its lock, one include.
 *
 * Same rxi API: log_trace/log_debug/log_info/log_warn/log_error/log_fatal, log_set_level/quiet/
 * add_fp/add_callback, etc. Output is fprintf -> stderr (log_lock_init() drops log.c's own
 * "HH:MM:SS" so the console's "[HH:MM:SS.mmm]" is the sole timestamp). Call log_lock_init() once
 * at startup. Select rxi's header explicitly: several vendor SDKs also export log.h.
 */
#include "log/src/log.h"   /* rxi log.h (log_* macros / log_set_lock / log_add_callback) */
#include "log_lock.h"      /* log_lock_init() — FreeRTOS-mutex thread-safety + no-timestamp reformat */

#endif /* AT_LOG_H */
