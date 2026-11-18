#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "bflb_mtimer.h"
#include "bflb_sec_trng.h"
#include "mm.h"

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t bytes,
                                 void *arg, UBaseType_t priority, TaskHandle_t *out, int core)
{
    if (core != 0 && core != tskNO_AFFINITY) return pdFAIL;
    if (priority >= configMAX_PRIORITIES) return pdFAIL;
    return xTaskCreate(fn, name, (bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t),
                       arg, priority, out);
}

uint32_t esp_log_timestamp(void) { return bflb_mtimer_get_time_ms(); }
int64_t esp_timer_get_time(void) { return bflb_mtimer_get_time_us(); }

void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)tag;
    if (level > CONFIG_LOG_DEFAULT_LEVEL) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return kfree_size(0);
}

size_t xPortGetFreeHeapSize(void) { return kfree_size(0); }

int ets_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int count = vprintf(fmt, args);
    va_end(args);
    return count;
}

size_t heap_caps_get_largest_free_block(unsigned caps)
{
    /* No equivalent query in this SDK; do not report total free as contiguous. */
    (void)caps;
    return 0;
}

void esp_fill_random(void *buffer, size_t length)
{
    configASSERT(length <= UINT32_MAX);
    configASSERT(bflb_trng_readlen(buffer, length) == 0);
}

uint32_t esp_random(void)
{
    uint32_t value;
    esp_fill_random(&value, sizeof(value));
    return value;
}

struct pan_timer {
    TimerHandle_t handle;
    esp_timer_cb_t callback;
    void *arg;
};

static void timer_callback(TimerHandle_t handle)
{
    struct pan_timer *timer = pvTimerGetTimerID(handle);
    timer->callback(timer->arg);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!args || !args->callback || !out) return ESP_ERR_INVALID_ARG;
    struct pan_timer *timer = calloc(1, sizeof(*timer));
    if (!timer) return ESP_ERR_NO_MEM;
    timer->callback = args->callback;
    timer->arg = args->arg;
    timer->handle = xTimerCreate(args->name ? args->name : "bd_alarm", 1, pdFALSE,
                                 timer, timer_callback);
    if (!timer->handle) { free(timer); return ESP_ERR_NO_MEM; }
    *out = timer;
    return ESP_OK;
}

static esp_err_t timer_start(esp_timer_handle_t timer, uint64_t us, BaseType_t periodic)
{
    if (!timer || !us) return ESP_ERR_INVALID_ARG;
    uint64_t ticks = (us * configTICK_RATE_HZ + 999999) / 1000000;
    if (ticks >= portMAX_DELAY) return ESP_ERR_INVALID_ARG;
    vTimerSetReloadMode(timer->handle, periodic);
    return xTimerChangePeriod(timer->handle, ticks ? ticks : 1, 0) == pdPASS
           ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t us)
{ return timer_start(timer, us, pdFALSE); }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t us)
{ return timer_start(timer, us, pdTRUE); }
esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    return xTimerStop(timer->handle, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

static void timer_free(void *timer, uint32_t unused)
{ (void)unused; free(timer); }

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (xTimerDelete(timer->handle, 0) != pdPASS) return ESP_FAIL;
    /* Free only after the service task has processed deletion and any callback
     * already in flight. Failure leaves memory allocated instead of a UAF. */
    return xTimerPendFunctionCall(timer_free, timer, 0, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{ return timer && xTimerIsTimerActive(timer->handle) == pdTRUE; }
