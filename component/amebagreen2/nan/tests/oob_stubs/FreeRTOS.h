#include <stdint.h>
typedef uint32_t TickType_t;
#define configTICK_RATE_HZ 1000u
#define pdMS_TO_TICKS(n) ((TickType_t)(n))
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
