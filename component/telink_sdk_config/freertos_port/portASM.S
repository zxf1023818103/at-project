/*
 * FreeRTOS Kernel V10.4.2
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 * 1 tab == 4 spaces!
 */

/* Adapted for TL3238: SDK vectored PLIC, full context on both UART and tick
 * interrupts, 16-byte stack alignment. Interrupt nesting remains disabled;
 * tickless WFI runs in the idle task without discarding its call stack.
 */
#define portWORD_SIZE 4
#define store_x sw
#define load_x lw
#define portCONTEXT_SIZE (30 * portWORD_SIZE)
#include "freertos_risc_v_chip_specific_extensions.h"
.section .text
.macro portasmSAVE_REGISTERS
    addi sp, sp, -portCONTEXT_SIZE
    store_x x1, 1 * portWORD_SIZE( sp )
    store_x x5, 2 * portWORD_SIZE( sp )
    store_x x6, 3 * portWORD_SIZE( sp )
    store_x x7, 4 * portWORD_SIZE( sp )
    store_x x8, 5 * portWORD_SIZE( sp )
    store_x x9, 6 * portWORD_SIZE( sp )
    store_x x10, 7 * portWORD_SIZE( sp )
    store_x x11, 8 * portWORD_SIZE( sp )
    store_x x12, 9 * portWORD_SIZE( sp )
    store_x x13, 10 * portWORD_SIZE( sp )
    store_x x14, 11 * portWORD_SIZE( sp )
    store_x x15, 12 * portWORD_SIZE( sp )
    store_x x16, 13 * portWORD_SIZE( sp )
    store_x x17, 14 * portWORD_SIZE( sp )
    store_x x18, 15 * portWORD_SIZE( sp )
    store_x x19, 16 * portWORD_SIZE( sp )
    store_x x20, 17 * portWORD_SIZE( sp )
    store_x x21, 18 * portWORD_SIZE( sp )
    store_x x22, 19 * portWORD_SIZE( sp )
    store_x x23, 20 * portWORD_SIZE( sp )
    store_x x24, 21 * portWORD_SIZE( sp )
    store_x x25, 22 * portWORD_SIZE( sp )
    store_x x26, 23 * portWORD_SIZE( sp )
    store_x x27, 24 * portWORD_SIZE( sp )
    store_x x28, 25 * portWORD_SIZE( sp )
    store_x x29, 26 * portWORD_SIZE( sp )
    store_x x30, 27 * portWORD_SIZE( sp )
    store_x x31, 28 * portWORD_SIZE( sp )

    csrr t0, mstatus                    /* Required for MPIE bit. */
    store_x t0, 29 * portWORD_SIZE( sp )

    portasmSAVE_ADDITIONAL_REGISTERS    /* Defined in freertos_risc_v_chip_specific_extensions.h to save any registers unique to the RISC-V implementation. */
    store_x x1, 0 * portWORD_SIZE( sp ) /* save ra as mepc for NONE irq switch in xPortYield function*/

    load_x  t0, pxCurrentTCB            /* Load pxCurrentTCB. */
    store_x  sp, 0( t0 )                /* Write sp to first TCB member. */
.endm

.macro portasmRESTORE_REGISTERS
    load_x  t1, pxCurrentTCB            /* Load pxCurrentTCB. */
    load_x  sp, 0( t1 )                 /* Read sp from first TCB member. */

    /* Load mret with the address of the next instruction in the task to run next. */
    load_x t0, 0( sp )
    csrw mepc, t0

    portasmRESTORE_ADDITIONAL_REGISTERS /* Defined in freertos_risc_v_chip_specific_extensions.h to restore any registers unique to the RISC-V implementation. */

    /* Load mstatus with the interrupt enable bits used by the task. */
    load_x  t0, 29 * portWORD_SIZE( sp )
    csrw mstatus, t0                        /* Required for MPIE bit. */

    load_x  x1, 1 * portWORD_SIZE( sp )
    load_x  x5, 2 * portWORD_SIZE( sp )     /* t0 */
    load_x  x6, 3 * portWORD_SIZE( sp )     /* t1 */
    load_x  x7, 4 * portWORD_SIZE( sp )     /* t2 */
    load_x  x8, 5 * portWORD_SIZE( sp )     /* s0/fp */
    load_x  x9, 6 * portWORD_SIZE( sp )     /* s1 */
    load_x  x10, 7 * portWORD_SIZE( sp )    /* a0 */
    load_x  x11, 8 * portWORD_SIZE( sp )    /* a1 */
    load_x  x12, 9 * portWORD_SIZE( sp )    /* a2 */
    load_x  x13, 10 * portWORD_SIZE( sp )   /* a3 */
    load_x  x14, 11 * portWORD_SIZE( sp )   /* a4 */
    load_x  x15, 12 * portWORD_SIZE( sp )   /* a5 */
    load_x  x16, 13 * portWORD_SIZE( sp )   /* a6 */
    load_x  x17, 14 * portWORD_SIZE( sp )   /* a7 */
    load_x  x18, 15 * portWORD_SIZE( sp )   /* s2 */
    load_x  x19, 16 * portWORD_SIZE( sp )   /* s3 */
    load_x  x20, 17 * portWORD_SIZE( sp )   /* s4 */
    load_x  x21, 18 * portWORD_SIZE( sp )   /* s5 */
    load_x  x22, 19 * portWORD_SIZE( sp )   /* s6 */
    load_x  x23, 20 * portWORD_SIZE( sp )   /* s7 */
    load_x  x24, 21 * portWORD_SIZE( sp )   /* s8 */
    load_x  x25, 22 * portWORD_SIZE( sp )   /* s9 */
    load_x  x26, 23 * portWORD_SIZE( sp )   /* s10 */
    load_x  x27, 24 * portWORD_SIZE( sp )   /* s11 */
    load_x  x28, 25 * portWORD_SIZE( sp )   /* t3 */
    load_x  x29, 26 * portWORD_SIZE( sp )   /* t4 */
    load_x  x30, 27 * portWORD_SIZE( sp )   /* t5 */
    load_x  x31, 28 * portWORD_SIZE( sp )   /* t6 */
    addi sp, sp, portCONTEXT_SIZE
.endm


/* Hardware automatically claims vectored PLIC interrupts. */
.macro RTOS_IRQ name, number
.global \name
.balign 4
\name:
    portasmSAVE_REGISTERS
    csrr t0, mepc
    sw t0, 0(sp)
    lw sp, xISRStackTop
    li a0, \number
    call vPortHandleInterrupt
    portasmRESTORE_REGISTERS
    mret
.endm
RTOS_IRQ entry_irq1, 1
RTOS_IRQ entry_irq3, 3
RTOS_IRQ entry_irq4, 4
RTOS_IRQ entry_irq5, 5
RTOS_IRQ entry_irq15, 15
RTOS_IRQ entry_irq19, 19

.global xPortYield
.balign 4
.func
xPortYield:
    csrrci  t0, mstatus,8   // disable interrupt, save mstatus to x5 (t0) //   It is OK to change t0, because xPortYield is a normal function
    andi    t0, t0, 8       // mask MIE bit
    slli    t0, t0, 4       // Copy the original MIE to MPIE
    li      t1, 0x80
    csrc    mstatus, t1     // Copy the original MIE to MPIE
    csrs    mstatus, t0     // Copy the original MIE to MPIE
    /* Unlike a hardware trap, a direct yield has not set MPP. TL3238 also
     * supports U-mode; returning with the post-mret MPP=0 would fault on flash.
     * Every task in this port runs in machine mode. */
    li      t1, 0x1800
    csrs    mstatus, t1
    portasmSAVE_REGISTERS
    load_x sp, xISRStackTop             /* Switch to ISR stack before function call. */
    jal vTaskSwitchContext
    portasmRESTORE_REGISTERS
    mret
.endfunc


.global xPortStartFirstTask
.balign 4
xPortStartFirstTask:
    csrci mstatus, 8
    portasmRESTORE_REGISTERS
    mret

.global pxPortInitialiseStack
.balign 4
.func
pxPortInitialiseStack:

    csrr t0, mstatus                    /* Obtain current mstatus value. */
    andi t0, t0, ~0x8                   /* Ensure interrupts are disabled when the stack is restored within an ISR.  Required when a task is created after the schedulre has been started, otherwise interrupts would be disabled anyway. */
    addi t1, x0, 0x188                  /* Generate the value 0x1880, which are the MPIE and MPP bits to set in mstatus. */
    slli t1, t1, 4
    or t0, t0, t1                       /* Set MPIE and MPP bits in mstatus value. */

    addi a0, a0, -portWORD_SIZE
    store_x t0, 0(a0)                   /* mstatus onto the stack. */
    addi a0, a0, -(22 * portWORD_SIZE)  /* Space for registers x11-x31. */
    store_x a2, 0(a0)                   /* Task parameters (pvParameters parameter) goes into register X10/a0 on the stack. */
    addi a0, a0, -(6 * portWORD_SIZE)   /* Space for registers x5-x9. */
    la t1, vPortTaskExitError
    store_x t1, 0(a0)
    addi t0, x0, portasmADDITIONAL_CONTEXT_SIZE /* The number of chip specific additional registers. */
chip_specific_stack_frame:              /* First add any chip specific registers to the stack frame being created. */
    beq t0, x0, 1f                      /* No more chip specific registers to save. */
    addi a0, a0, -portWORD_SIZE         /* Make space for chip specific register. */
    store_x x0, 0(a0)                   /* Give the chip specific register an initial value of zero. */
    addi t0, t0, -1                     /* Decrement the count of chip specific registers remaining. */
    j chip_specific_stack_frame         /* Until no more chip specific registers. */
1:
    addi a0, a0, -portWORD_SIZE
    store_x a1, 0(a0)                   /* mret value (pxCode parameter) onto the stack. */
    ret
    .endfunc



.global trap_entry
.balign 4
trap_entry:
    csrci mstatus, 8
    lw sp, xISRStackTop
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mtval
    call vPortTrap
1:  j 1b
