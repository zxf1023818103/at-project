#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct pan_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);
typedef enum { ESP_TIMER_TASK } esp_timer_dispatch_t;
typedef struct { esp_timer_cb_t callback; void *arg; esp_timer_dispatch_t dispatch_method; const char *name; bool skip_unhandled_events; } esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
esp_err_t esp_timer_start_once(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
bool esp_timer_is_active(esp_timer_handle_t);
int64_t esp_timer_get_time(void);
