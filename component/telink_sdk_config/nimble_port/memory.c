/* SPDX-License-Identifier: Apache-2.0 */
#include "FreeRTOS.h"
#include <stdint.h>
#include <string.h>

/* NimBLE's realloc needs the old allocation size. A 16-byte prefix preserves
 * the port's alignment. Every allocation belongs to the FreeRTOS heap. */
void *tl3238_nimble_malloc(size_t size)
{
    if (size > SIZE_MAX - 16u) return NULL;
    size_t *p = pvPortMalloc(size + 16u);
    if (!p) return NULL;
    *p = size;
    return (uint8_t *)p + 16u;
}

void tl3238_nimble_free(void *p)
{
    if (p) vPortFree((uint8_t *)p - 16u);
}

void *tl3238_nimble_calloc(size_t n, size_t size)
{
    if (size && n > SIZE_MAX / size) return NULL;
    void *p = tl3238_nimble_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void *tl3238_nimble_realloc(void *p, size_t size)
{
    if (!size) { tl3238_nimble_free(p); return NULL; }
    void *q = tl3238_nimble_malloc(size);
    if (q && p) {
        size_t old = *(size_t *)((uint8_t *)p - 16u);
        memcpy(q, p, old < size ? old : size);
        tl3238_nimble_free(p);
    }
    return q;
}
