/*
 * FreeRTOS_POSIX_portable.h — at-project port config for FreeRTOS-Plus-POSIX
 * (component freertos-posix_config).
 *
 * FreeRTOS_POSIX.h includes THIS (the user/port overrides) and then
 * FreeRTOS_POSIX_portable_default.h (the shipped defaults). So this only needs to carry
 * overrides — empty means every posixconfig* macro falls back to the default. Override
 * the defaults here (e.g. posixconfigPTHREAD_TASK_NAME, the time type) as consumers need.
 */
#ifndef _FREERTOS_POSIX_PORTABLE_H_
#define _FREERTOS_POSIX_PORTABLE_H_

/* Defaults are sufficient; add posixconfig* overrides below. */

#endif /* _FREERTOS_POSIX_PORTABLE_H_ */
