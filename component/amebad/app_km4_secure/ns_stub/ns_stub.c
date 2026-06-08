/*
 * Minimal AmebaD KM4 Non-Secure stub for TF-M NSPE bring-up.
 *
 * TF-M's SPM, after secure init, reads this stub's vector table at NS_CODE_START
 * (MSP=[0], entry=[1]) and BLXNS-es to the entry in the Non-Secure state. The
 * stub just prints "NS!" via the NS ROM LOGUART and spins — enough to prove the
 * full Secure->Non-Secure handoff works on hardware.
 *
 * RAM-resident: the bootloader copies ns_stub.bin to NS_CODE_START (0x10010000,
 * inside the SAU Non-Secure SRAM region). Built freestanding, linked by
 * ns_stub.ld; no libc/crt.
 */
#include <stdint.h>

/* Non-Secure ROM LOGUART_PutChar (NS alias; the bootloader already init'd it). */
static void ns_putc(char c) { ((void (*)(unsigned char))0x1010caf5u)((unsigned char)c); }

__attribute__((noreturn)) void ns_reset(void)
{
    ns_putc('N'); ns_putc('S'); ns_putc('!'); ns_putc('\n');
    for (;;) { }
}

/* NS vector table: [0]=initial MSP_NS, [1]=reset entry (thumb). */
extern uint32_t __ns_stack_top__;
__attribute__((used, section(".ns_vectors")))
const void *const ns_vt[2] = {
    (const void *)0x10020000u,        /* MSP_NS top (NS SRAM) */
    (const void *)ns_reset,           /* reset (thumb bit set by compiler) */
};
