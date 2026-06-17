#ifndef AT_LOG_H
#define AT_LOG_H

/*
 * at-project logging — rxi's log.c (component/log_config/log) behind a uniquely-named header.
 *
 * The upstream header is the very generic "log.h", which collides with other components' log.h
 * (e.g. tlsf_config/port_generic/log.h on the heap lib's include path). So log_config does NOT
 * put log/src on the public include path; consumers include "at_log.h" instead. Same rxi API:
 * log_trace/log_debug/log_info/log_warn/log_error/log_fatal, log_set_level/quiet/add_fp, etc.
 * Output stays the upstream default (fprintf -> stderr). Call log_lock_init() once at startup.
 */
#include "log/src/log.h"   /* rxi log.h — relative to this file's dir (component/log_config/) */
#include "log_lock.h"      /* log_lock_init() — FreeRTOS-mutex thread-safety */

#endif /* AT_LOG_H */
