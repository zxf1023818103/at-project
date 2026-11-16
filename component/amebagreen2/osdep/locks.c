/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/lock.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "os_wrapper.h"
#include "log.h"

/* Refer to stdlib.h, stdio.h, time.h */
StaticSemaphore_t __lock___sinit_recursive_mutex; // newlib 4.1.0 still use
StaticSemaphore_t __lock___sfp_recursive_mutex; // e.g. __sinit, called by vfprintf
StaticSemaphore_t __lock___atexit_recursive_mutex; // e.g. called by atexit
StaticSemaphore_t __lock___env_recursive_mutex; // e.g. called by getenv, setenv
StaticSemaphore_t __lock___tz_mutex; // e.g. called by mktime, localtime

StaticSemaphore_t __lock___malloc_recursive_mutex;
StaticSemaphore_t __lock___at_quick_exit_mutex;
StaticSemaphore_t __lock___dd_hash_mutex;
StaticSemaphore_t __lock___arc4random_mutex;

static const char *const TAG = "LOCKS";


// __attribute__((constructor))
static void init_retarget_locks(void)
{
#ifdef CONFIG_ARM_CORE_CM4_KM4TZ
	RTK_LOGI(TAG, "KM4TZ %s\n", __func__);
#else
	RTK_LOGI(TAG, "KM4NS %s\n", __func__);
#endif
#if(configSUPPORT_STATIC_ALLOCATION == 1)
	xSemaphoreCreateRecursiveMutexStatic(&__lock___sinit_recursive_mutex);
	xSemaphoreCreateRecursiveMutexStatic(&__lock___sfp_recursive_mutex);
	xSemaphoreCreateRecursiveMutexStatic(&__lock___atexit_recursive_mutex);
	xSemaphoreCreateRecursiveMutexStatic(&__lock___env_recursive_mutex);
	xSemaphoreCreateMutexStatic(&__lock___tz_mutex);
    xSemaphoreCreateRecursiveMutexStatic(&__lock___malloc_recursive_mutex);
    xSemaphoreCreateMutexStatic(&__lock___at_quick_exit_mutex);
    xSemaphoreCreateMutexStatic(&__lock___dd_hash_mutex);
    xSemaphoreCreateMutexStatic(&__lock___arc4random_mutex);
#endif
}

void __retarget_lock_init(_LOCK_T *lock_ptr)
{
    /* newlib reuses FILE slots after fclose; the old lock value may be a
     * freed handle. Init must always install a fresh mutex. */
	*lock_ptr = (_LOCK_T)xSemaphoreCreateMutex();

	if (*lock_ptr == NULL) {
		rtk_assert(*lock_ptr);
	}
}

void __retarget_lock_init_recursive(_LOCK_T *lock_ptr)
{
    /* newlib reuses FILE slots after fclose; the old lock value may be a
     * freed handle. Init must always install a fresh mutex. */
	*lock_ptr = (_LOCK_T)xSemaphoreCreateRecursiveMutex();

	if (*lock_ptr == NULL) {
		rtk_assert(*lock_ptr);
	}
}

void __retarget_lock_close(_LOCK_T lock)
{
	if (lock) {
		vSemaphoreDelete(lock);
	}
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
	if (lock) {
		rtk_assert(xSemaphoreGetMutexHolder((QueueHandle_t)lock) == NULL);
		vSemaphoreDelete(lock);
	}
}

void __retarget_lock_acquire(_LOCK_T lock)
{
	BaseType_t ret;
	BaseType_t task_woken = pdFALSE;

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return;
	}

	rtk_assert(lock);

	if (rtos_critical_is_in_interrupt()) {
		ret = xSemaphoreTakeFromISR(lock, &task_woken);
		if (ret != pdTRUE) {
			rtk_assert(0);
		}
		portEND_SWITCHING_ISR(task_woken);
	} else {
		ret = xSemaphoreTake((QueueHandle_t)lock, portMAX_DELAY);
		if (ret != pdTRUE) {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "%p acq failed\n", lock);
		}
	}
}

void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
	BaseType_t ret;

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return;
	}

	rtk_assert(lock);

	if (rtos_critical_is_in_interrupt()) {
		rtk_assert(0);
	}

	ret = xSemaphoreTakeRecursive((QueueHandle_t)lock, portMAX_DELAY);
	if (ret != pdTRUE) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "%p acq recur failed\n", lock);
	}
}

int __retarget_lock_try_acquire(_LOCK_T lock)
{
	BaseType_t ret;
	BaseType_t task_woken = pdFALSE;

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return 0;
	}

	rtk_assert(lock);

	if (rtos_critical_is_in_interrupt()) {
		ret = xSemaphoreTakeFromISR(lock, &task_woken);
		if (task_woken) {
			portEND_SWITCHING_ISR(task_woken);
		}
	} else {
		ret = xSemaphoreTake((QueueHandle_t)lock, 0);
		if (ret != pdTRUE) {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "%p try acq failed\n", lock);
		}
	}

	return (ret == pdTRUE) ? 0 : -1;
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
	BaseType_t ret;

	if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
		return 0;
	}

	rtk_assert(lock);

	if (rtos_critical_is_in_interrupt()) {
		rtk_assert(0);
	}

	ret = xSemaphoreTakeRecursive((QueueHandle_t)lock, 0);
	if (ret != pdTRUE) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "%p try acq recur failed\n", lock);
	}
	return (ret == pdTRUE) ? 0 : -1;
}

void __retarget_lock_release(_LOCK_T lock)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
	BaseType_t ret;
	BaseType_t task_woken = pdFALSE;

	if (!lock) {
		return;
	}

	if (rtos_critical_is_in_interrupt()) {
		xSemaphoreGiveFromISR(lock, &task_woken);
		if (task_woken) {
			portEND_SWITCHING_ISR(task_woken);
		}
	} else {
		ret = xSemaphoreGive(lock);
		if (ret != pdTRUE) {
			RTK_LOGS(TAG, RTK_LOG_ERROR, "%p rls failed\n", lock);
		}
	}
}

void __retarget_lock_release_recursive(_LOCK_T lock)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
	BaseType_t ret;

	if (!lock) {
		return;
	}

	if (rtos_critical_is_in_interrupt()) {
		rtk_assert(0);
	}

	ret = xSemaphoreGiveRecursive((QueueHandle_t)lock);
	if (ret != pdTRUE) {
		RTK_LOGS(TAG, RTK_LOG_ERROR, "%p rls recur failed\n", lock);
	}
}

void newlib_locks_init(void)
{
	init_retarget_locks();
}