#pragma once
#include "../../tl3238/config/FreeRTOSConfig.h"
#undef configMAX_PRIORITIES
#define configMAX_PRIORITIES 8
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE (76 * 1024)
#undef configTIMER_TASK_PRIORITY
#define configTIMER_TASK_PRIORITY 6
#undef configTIMER_QUEUE_LENGTH
#define configTIMER_QUEUE_LENGTH 32
#undef configTIMER_TASK_STACK_DEPTH
#define configTIMER_TASK_STACK_DEPTH 768
#define INCLUDE_xTimerPendFunctionCall 1
