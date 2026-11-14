/********************************************************************************************************
 * @file    trap_entry.h
 *
 * @brief   This is the header file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    02,2024
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/

#ifndef APP_TRAP_IRQ_H_
#define APP_TRAP_IRQ_H_

#include "vendor/common/user_config.h"

#ifdef TRAP_ENTRY_ENABLE
#ifndef __ASSEMBLER__
#include "common/types.h"
#endif
    /* Save RISCV core GPRs, FPU and additional CSR registers */
    #define SAVE_FPU_AND_ADDITIONAL_REG_EN 0

    #if (SAVE_FPU_AND_ADDITIONAL_REG_EN)
        /* RISCV core support FPU */
        #ifdef __riscv_flen
            #define ARCH_RISCV_FPU
            #define ARCH_RISCV_FPU_S
        #endif
    #endif

    /* Bytes of register width  */
    #ifdef ARCH_CPU_64BIT
        #define STORE    sd
        #define LOAD     ld
        #define REGBYTES 8
    #else
        #define STORE    sw
        #define LOAD     lw
        #define REGBYTES 4
    #endif

    /* FPU */
    #ifdef ARCH_RISCV_FPU
        #ifdef ARCH_RISCV_FPU_D
            #define FSTORE        fsd
            #define FLOAD         fld
            #define FREGBYTES     8
            #define rv_floatreg_t s64
        #endif
        #ifdef ARCH_RISCV_FPU_S
            #define FSTORE        fsw
            #define FLOAD         flw
            #define FREGBYTES     4
            #define rv_floatreg_t s32
        #endif
    #endif

    #if (SAVE_FPU_AND_ADDITIONAL_REG_EN)
        /* Constants to define the additional CSRs. */
        #define mxstatus 0x7c4
        #define ucode    0x801

        /* Number of additional CSR registers to save and restore. */
        #define ADDITIONAL_CSR_SIZE 2
    #endif

    /* Global buffer for all GPRs, available even when printing is not possible.
     * Layout matches trap_entry.S stack frame:
     *   [0] = mepc, [1] = x1(ra), [2] = x2(sp), ..., [31] = x31
     */
#ifndef __ASSEMBLER__
    extern volatile u32 g_trap_gpr_context[32];

    /* Weak hook for interrupt dispatch.
     * RTOS (e.g. FreeRTOS) can override this to route interrupts
     * to its own handler table. Default implementation returns mepc unchanged.
     */
    u32 trap_irq_dispatch(u32 mcause, u32 mepc, u32 *sp);

    /* Configurable trap print macro.
     * Users can override TRAP_PRINTF before including this header to redirect
     * output to different backends (tlkapi, bare UART, semihosting, etc.).
     * Default: tlkapi_printf with RISCV_TRAP_DEBUG_EN gate.
     */
    #ifndef TRAP_PRINTF
        #ifndef RISCV_TRAP_DEBUG_EN
            #define RISCV_TRAP_DEBUG_EN 1
        #endif
        #define TRAP_PRINTF(fmt, ...)  tlkapi_printf(RISCV_TRAP_DEBUG_EN, fmt, ##__VA_ARGS__)
    #endif

    /* Backtrace support switch.
     * Set to 1 to enable rvbacktrace_fomit() call in trap_handler.
     * Requires rvbacktrace module to be available.
     */
    #ifndef TRAP_BACKTRACE_EN
        #define TRAP_BACKTRACE_EN 0
    #endif
    #if (TRAP_BACKTRACE_EN)
        #include "vendor/common/backtrace/rvbacktrace/rvbacktrace.h"
    #endif
#endif /* __ASSEMBLER__ */

#endif /* TRAP_ENTRY_ENABLE */

#endif /* APP_TRAP_IRQ_H_ */
