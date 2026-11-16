/* Native test shim only. Firmware uses the real kernel headers. */
#ifndef NAN_TEST_FREERTOS_H
#define NAN_TEST_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(n) (n)
#endif
