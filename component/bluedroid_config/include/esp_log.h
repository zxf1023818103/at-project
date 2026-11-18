#pragma once
#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
#define ESP_LOGE(tag, fmt, ...) printf("E %s: " fmt "\r\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W %s: " fmt "\r\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("I %s: " fmt "\r\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_LOG_BUFFER_HEX(...) ((void)0)
#define ESP_LOG_BUFFER_HEX_LEVEL(...) ((void)0)
#define ESP_LOG_BUFFER_CHAR(...) ((void)0)
uint32_t esp_log_timestamp(void);
#define LOG_FORMAT(letter, format) #letter " (%lu) %s: " format "\r\n"
void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...);
