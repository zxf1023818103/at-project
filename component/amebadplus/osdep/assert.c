#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"

/* Avoid libc locks/heap in the fatal path. Caller and stack candidates can be
 * symbolized with arm-none-eabi-addr2line against the matching image ELF. */
void amebadplus_assert(const char *file, unsigned line, const char *expr, void *caller)
{
    __disable_irq();
    DiagPrintf("\nASSERT %s:%u %s caller=%p\n", file, line, expr, caller);
    uint32_t *sp;
    __asm volatile ("mrs %0, psp" : "=r" (sp));
    DiagPrintf("stack=%p\n", sp);
    for (unsigned i = 0; i < 48 && (uintptr_t)(sp+i) >= 0x20000000 &&
         (uintptr_t)(sp+i) < 0x20080000; ++i) {
        uint32_t v = sp[i];
        if ((v >= 0x0c000000 && v < 0x0e200000) || (v >= 0x20000000 && v < 0x20080000))
            DiagPrintf("[%u]=%08x ", i, v);
    }
    DiagPrintf("\n");
    for (;;) {}
}
