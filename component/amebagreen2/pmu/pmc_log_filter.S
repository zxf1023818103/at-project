/* Suppress only the vendor PMC's exact APCG/APCW progress messages.
 * Tail-call the ROM logger for everything else: r0-r3, stack arguments and
 * LR must remain intact because rtk_log_write_nano() is variadic. Use only
 * the AAPCS scratch register r12 and keep this entry in SRAM for PMC callers.
 */
.syntax unified
.thumb
.section .sram.only.text.pmc_log_filter,"ax",%progbits
.p2align 2
.global __wrap_rtk_log_write_nano
.type __wrap_rtk_log_write_nano, %function
.thumb_func
__wrap_rtk_log_write_nano:
    cmp     r0, #1                  /* RTK_LOG_ALWAYS */
    bne     .Lforward
    cbz     r3, .Lforward
    ldrb    r12, [r3, #0]
    cmp     r12, #'A'
    bne     .Lforward
    ldrb    r12, [r3, #1]
    cmp     r12, #'P'
    bne     .Lforward
    ldrb    r12, [r3, #2]
    cmp     r12, #'C'
    bne     .Lforward
    ldrb    r12, [r3, #3]
    cmp     r12, #'G'
    beq     .Lending
    cmp     r12, #'W'
    bne     .Lforward
.Lending:
    ldrb    r12, [r3, #4]
    cmp     r12, #10                 /* newline */
    bne     .Lforward
    ldrb    r12, [r3, #5]
    cmp     r12, #0
    bne     .Lforward
    bx      lr
.Lforward:
    b       __real_rtk_log_write_nano
.size __wrap_rtk_log_write_nano, . - __wrap_rtk_log_write_nano
.section .note.GNU-stack,"",%progbits
