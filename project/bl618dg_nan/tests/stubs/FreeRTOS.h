#pragma once
#include <assert.h>
#include <stdlib.h>
#define configASSERT(x) assert(x)
#define pvPortMalloc(n) malloc(n)
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
