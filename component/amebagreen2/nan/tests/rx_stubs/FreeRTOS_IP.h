#ifndef TEST_FREERTOS_IP_H
#define TEST_FREERTOS_IP_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
typedef struct xNETWORK_BUFFER {
    struct xNETWORK_BUFFER *pxNextBuffer;
    unsigned test_id;
} NetworkBufferDescriptor_t;
#endif
