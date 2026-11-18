#include "FreeRTOS_IP.h"
enum { eNetworkRxEvent = 1 };
typedef struct { int eEventType; void *pvData; } IPStackEvent_t;
BaseType_t xSendEventStructToIPTask(const IPStackEvent_t *event, TickType_t wait);
