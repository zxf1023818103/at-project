/*
 * osif_freertos.c — minimal Realtek "osif" RTOS shim over FreeRTOS, trimmed to
 * the surface the driver-only HCI controller bring-up actually links.
 *
 * The original SDK file exported 49 osif_* wrappers; this driver-only image uses
 * the BT controller transport only (no Realtek upper stack / btgap.a, host =
 * NimBLE which has its own porting layer), so just 10 are referenced — by
 * hci_uart.c and hci_nostack.c. The rest (timers, mutexes, signals, msg queues,
 * scheduler control, lock/unlock, aligned alloc, sys_time) had no caller and were
 * dropped. Re-add a wrapper here if a future consumer needs it; the FreeRTOS call
 * it maps to is one line.
 *
 * Semantics preserved from the SDK: sem_take/sem_give pick the FromISR variant in
 * ISR context; wait_ms == 0xFFFFFFFF means block forever (portMAX_DELAY);
 * osif_mem_* ignore RAM_TYPE and use the single FreeRTOS heap. KM4 only.
 */
#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <cmsis_gcc.h>   /* __get_IPSR */

#include <osif.h>

/* task context (true) vs isr context (false) — IPSR==0 means thread mode. */
static inline bool osif_task_context_check(void)
{
    return (__get_IPSR() == 0);
}

/* ---- schedule ---- */

void osif_delay(uint32_t ms)
{
    vTaskDelay((TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS));
}

/* ---- task ---- */

bool osif_task_create(void **pp_handle, const char *p_name, void (*p_routine)(void *),
                      void *p_param, uint16_t stack_size, uint16_t priority)
{
    if (pp_handle == NULL) {
        return false;
    }
    return xTaskCreate(p_routine, (const char *)p_name, stack_size / sizeof(portSTACK_TYPE),
                       p_param, priority, (TaskHandle_t *)pp_handle) == pdPASS;
}

bool osif_task_delete(void *p_handle)
{
    vTaskDelete((TaskHandle_t)p_handle);
    return true;
}

/* ---- synchronization ---- */

bool osif_sem_create(void **pp_handle, uint32_t init_count, uint32_t max_count)
{
    if (pp_handle == NULL) {
        return false;
    }
    *pp_handle = (void *)xSemaphoreCreateCounting(max_count, init_count);
    return (*pp_handle != NULL);
}

bool osif_sem_delete(void *p_handle)
{
    vSemaphoreDelete((QueueHandle_t)p_handle);
    return true;
}

bool osif_sem_take(void *p_handle, uint32_t wait_ms)
{
    BaseType_t ret;

    if (osif_task_context_check()) {
        TickType_t wait_ticks = (wait_ms == 0xFFFFFFFFUL)
                              ? portMAX_DELAY
                              : (TickType_t)((wait_ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
        ret = xSemaphoreTake((QueueHandle_t)p_handle, wait_ticks);
    } else {
        BaseType_t task_woken = pdFALSE;
        ret = xSemaphoreTakeFromISR((QueueHandle_t)p_handle, &task_woken);
        portEND_SWITCHING_ISR(task_woken);
    }
    return (ret == pdTRUE);
}

bool osif_sem_give(void *p_handle)
{
    BaseType_t ret;

    if (osif_task_context_check()) {
        ret = xSemaphoreGive((QueueHandle_t)p_handle);
    } else {
        BaseType_t task_woken = pdFALSE;
        ret = xSemaphoreGiveFromISR((QueueHandle_t)p_handle, &task_woken);
        portEND_SWITCHING_ISR(task_woken);
    }
    return (ret == pdTRUE);
}

/* ---- memory (single FreeRTOS heap; RAM_TYPE ignored) ---- */

void *osif_mem_alloc(RAM_TYPE ram_type, size_t size)
{
    (void)ram_type;
    return pvPortMalloc(size);
}

void osif_mem_free(void *p_block)
{
    if (p_block == NULL) {
        return;
    }
    vPortFree(p_block);
}

size_t osif_mem_peek(RAM_TYPE ram_type)
{
    (void)ram_type;
    return xPortGetFreeHeapSize();
}
