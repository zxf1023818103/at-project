/**
 * Copyright (c) 2017, Realtek Semiconductor Corporation. All rights reserved.
 *
 * Trimmed to the two entries the vendored HCI transport actually uses:
 * os_mem_zalloc (alloc + clear) and os_mem_free. Both resolve to FreeRTOS heap
 * via hci_nostack.c (os_mem_zalloc_intern -> osif_mem_alloc -> pvPortMalloc;
 * os_mem_free -> osif_mem_free -> vPortFree). The SDK's os_mem_alloc /
 * aligned_alloc / mem_peek and the os_pool/os_sched/os_sync APIs had no caller in
 * this driver-only image and were dropped.
 */

#ifndef _OS_MEM_H_
#define _OS_MEM_H_

#include <stdint.h>
#include <stddef.h>
#include <mem_types.h>

#ifdef __cplusplus
extern "C" {
#endif

void *os_mem_zalloc_intern(RAM_TYPE ram_type, size_t size,
                           const char *p_func, uint32_t file_line);

/* Allocate a zero-initialised block from the FreeRTOS heap (RAM_TYPE ignored). */
#define os_mem_zalloc(ram_type, size)   \
    os_mem_zalloc_intern(ram_type, size, __func__, __LINE__)

/* Free a block obtained from os_mem_zalloc. */
void os_mem_free(void *p_block);

#ifdef __cplusplus
}
#endif

#endif /* _OS_MEM_H_ */
