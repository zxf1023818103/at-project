/*
 * tfm_os_wrapper_freertos.c — minimal os_wrapper (mutex) over FreeRTOS.
 *
 * TF-M's serialized NS interface (tfm_ns_interface_rtos.c) wraps EVERY NS->SPE PSA call in
 * os_wrapper_mutex_acquire()/release() so only one NS task is ever inside a secure call at a
 * time (the SPE's SFN partitions share a secure stack — concurrent NS callers would corrupt it).
 * TF-M ships the os_wrapper *header* but no implementation; this provides the FreeRTOS one.
 *
 * acquire/release tolerate a NULL handle (return SUCCESS) so the pre-scheduler, single-threaded
 * boot PSA self-tests work even before tfm_ns_interface_init() runs.
 */
#include <stdint.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "os_wrapper/common.h"
#include "os_wrapper/mutex.h"

void *os_wrapper_mutex_create(void)
{
    return (void *)xSemaphoreCreateMutex();
}

uint32_t os_wrapper_mutex_acquire(void *handle, uint32_t timeout)
{
    TickType_t ticks;

    if (handle == NULL) {
        return OS_WRAPPER_SUCCESS;   /* pre-init / single-threaded boot: no serialization needed */
    }
    ticks = (timeout == OS_WRAPPER_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);
    return (xSemaphoreTake((SemaphoreHandle_t)handle, ticks) == pdTRUE)
               ? OS_WRAPPER_SUCCESS : OS_WRAPPER_ERROR;
}

uint32_t os_wrapper_mutex_release(void *handle)
{
    if (handle == NULL) {
        return OS_WRAPPER_SUCCESS;
    }
    return (xSemaphoreGive((SemaphoreHandle_t)handle) == pdTRUE)
               ? OS_WRAPPER_SUCCESS : OS_WRAPPER_ERROR;
}
