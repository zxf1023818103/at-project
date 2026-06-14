/*
 * secure_demo.h — the NSC (non-secure-callable) veneer surface exported by the
 * AmebaD KM4 standalone FreeRTOS secure image, shared with the NS preemption test
 * (component/amebad/app_km4/freertos_secure_test.c).
 *
 * Both sides include this so the secure-stack buffer pattern is defined ONCE: the
 * NS test recomputes the expected checksum in plain C and compares it to what the
 * secure veneer returns after a deliberately long, preemptible run. A mismatch
 * means the per-task secure stack was NOT saved/restored across preemption.
 */
#ifndef AMEBAD_FREERTOS_SECURE_DEMO_H
#define AMEBAD_FREERTOS_SECURE_DEMO_H

#include <stdint.h>

/* Words of secure-stack scratch the veneer fills/checksums. 48 words = 192 bytes,
 * comfortably inside configMINIMAL_SECURE_STACK_SIZE (1024) with frame overhead. */
#define SECURE_DEMO_BUF_WORDS   48u

/* The exact pattern the veneer writes into buf[i] — recomputed NS-side to verify. */
#define SECURE_DEMO_PATTERN(seed, tag, i) \
    ( (uint32_t)(seed) ^ ( (uint32_t)(i) * 0x9E3779B1u ) ^ ( (uint32_t)(tag) << 16 ) )

/*
 * Fill SECURE_DEMO_BUF_WORDS of secure-stack scratch from (seed,tag), burn `spin`
 * nop iterations (long enough that an NS SysTick lands mid-call and the kernel may
 * switch tasks), then return the checksum of the buffer. If FreeRTOS preserved
 * this task's secure stack across the preemption, the checksum equals the NS-side
 * recomputation; if a preempting task's secure call clobbered a shared stack, it
 * won't.
 */
uint32_t secure_demo_compute(uint32_t seed, uint32_t tag, uint32_t spin);

/* Return the current secure PSP — lets the NS side show the two tasks run on
 * DISTINCT secure stacks (direct per-task-secure-stack evidence). */
uint32_t secure_demo_psp(void);

#endif /* AMEBAD_FREERTOS_SECURE_DEMO_H */
