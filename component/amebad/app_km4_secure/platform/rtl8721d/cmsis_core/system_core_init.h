#ifndef __SYSTEM_CORE_INIT_H__
#define __SYSTEM_CORE_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern uint32_t SystemCoreClock;

void SystemInit(void);
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_CORE_INIT_H__ */
