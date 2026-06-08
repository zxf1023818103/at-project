/**
 * Copyright (c) 2015, Realsil Semiconductor Corporation. All rights reserved.
 *
 * Trimmed to the 7 wrappers the driver-only HCI bring-up actually uses (see
 * osif_freertos.c): one delay, task create/delete, and the counting semaphore.
 * Everything else (timers, mutexes, signals, msg queues, scheduler control,
 * lock/unlock, memory — see below) was dropped. The SDK's os_mem_* used to route
 * through osif_mem_*, but those were pure FreeRTOS skins, so callers now use
 * pvPortMalloc / vPortFree / xPortGetFreeHeapSize directly.
 */

#ifndef _OSIF_H_
#define _OSIF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* schedule */
void osif_delay(uint32_t ms);

/* task */
bool osif_task_create(void **pp_handle, const char *p_name, void (*p_routine)(void *),
                      void *p_param, uint16_t stack_size, uint16_t priority);
bool osif_task_delete(void *p_handle);

/* synchronization */
bool osif_sem_create(void **pp_handle, uint32_t init_count, uint32_t max_count);
bool osif_sem_delete(void *p_handle);
bool osif_sem_take(void *p_handle, uint32_t wait_ms);
bool osif_sem_give(void *p_handle);

#ifdef __cplusplus
}
#endif

#endif /* _OSIF_H_ */
