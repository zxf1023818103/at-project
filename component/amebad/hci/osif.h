/**
 * Copyright (c) 2015, Realsil Semiconductor Corporation. All rights reserved.
 *
 * Trimmed to the 12 wrappers the driver-only HCI bring-up actually uses (see
 * osif_freertos.c). The SDK's full 49-entry surface (timers, mutexes, signals,
 * msg queues, scheduler control, ...) was dropped — no caller in this image.
 */

#ifndef _OSIF_H_
#define _OSIF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <mem_types.h>

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
uint32_t osif_lock(void);
void osif_unlock(uint32_t flags);
bool osif_sem_create(void **pp_handle, uint32_t init_count, uint32_t max_count);
bool osif_sem_delete(void *p_handle);
bool osif_sem_take(void *p_handle, uint32_t wait_ms);
bool osif_sem_give(void *p_handle);

/* memory */
void *osif_mem_alloc(RAM_TYPE ram_type, size_t size);
void osif_mem_free(void *p_block);
size_t osif_mem_peek(RAM_TYPE ram_type);

#ifdef __cplusplus
}
#endif

#endif /* _OSIF_H_ */
