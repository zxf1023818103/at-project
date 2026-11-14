/* SPDX-License-Identifier: MIT */
#include "FreeRTOS.h"
#include "task.h"
#include "driver.h"
#include "tl3238_port.h"

#ifdef TL3238_ENABLE_CS
static StackType_t isr_stack[configISR_STACK_SIZE_WORDS] __attribute__((section(".iram_bss"), aligned(16)));
#else
static StackType_t isr_stack[configISR_STACK_SIZE_WORDS] __attribute__((aligned(16)));
#endif
const StackType_t xISRStackTop = (StackType_t)&isr_stack[configISR_STACK_SIZE_WORDS];
static unsigned int next_tick;
volatile BaseType_t tl3238_isr_active, tl3238_isr_yield;
#ifdef TL3238_ENABLE_BLE
extern void tl3238_controller_irq(void);
#ifdef TL3238_ENABLE_CS
extern void tl3238_controller_cs_irq(void);
#define TICK_TIMER TIMER1
#define TICK_IRQ IRQ_TIMER1
#define TICK_MASK FLD_TMR1_MODE_IRQ
#else
#define TICK_TIMER TIMER0
#define TICK_IRQ IRQ_TIMER0
#define TICK_MASK FLD_TMR0_MODE_IRQ
#endif
#define tick_pending() timer_get_irq_status(TICK_MASK)
#define clear_tick() timer_clr_irq_status(TICK_MASK)
#else
#define TICK_IRQ IRQ_SYSTIMER
#define tick_pending() stimer_get_irq_status(FLD_SYSTEM_IRQ)
#define clear_tick() stimer_clr_irq_status(FLD_SYSTEM_IRQ)
#endif
static volatile Tl3238TicklessStats tickless_stats = {.enabled = configUSE_TICKLESS_IDLE};

extern void xPortStartFirstTask(void);
extern BaseType_t console_rx_isr(void);
extern BaseType_t console_rx_pending(void);
extern void console_write(const char *text);

#define TICK_CYCLES (SYSTEM_TIMER_TICK_1S / configTICK_RATE_HZ)
#define MAX_IDLE_TICKS pdMS_TO_TICKS(TL3238_MAX_IDLE_MS)
#if (24000000 % configTICK_RATE_HZ) != 0
#error STIMER clock must be divisible by configTICK_RATE_HZ
#endif

/* Reprogramming a compare after its deadline can miss the hardware match.
 * Pend the normal ISR in that case, including a boundary crossed during MMIO.
 * The ISR checks time rather than assuming every pending request is a tick. */
static void arm_deadline(uint32_t deadline)
{
#ifdef TL3238_ENABLE_BLE
    /* STIMER compare belongs exclusively to the BLE controller. TICK_TIMER uses
     * PCLK, while the 24 MHz STIMER counter remains our monotonic timebase. */
    timer_stop(TICK_TIMER);
    clear_tick();
    timer_set_init_tick(TICK_TIMER, 0);
    int32_t remaining = (int32_t)(deadline - stimer_get_tick());
    uint32_t cycles = remaining > 0 ?
        (uint32_t)((uint64_t)(uint32_t)remaining * sys_clk.pclk / 24u) : 1u;
    timer_set_cap_tick(TICK_TIMER, cycles ? cycles : 1u);
    timer_start(TICK_TIMER);
#else
    stimer_set_irq_capture(deadline);
#endif
    __asm volatile("fence iorw, iorw" ::: "memory");
    if ((int32_t)(stimer_get_tick() - deadline) >= 0) {
        plic_set_pending(TICK_IRQ);
    }
}

static void arm_tick_interrupt(void) { arm_deadline(next_tick); }

void tl3238_tickless_enable(uint32_t enabled)
{
    taskENTER_CRITICAL();
    tickless_stats.enabled = (configUSE_TICKLESS_IDLE != 0) && (enabled != 0);
    taskEXIT_CRITICAL();
}

void tl3238_tickless_get_stats(Tl3238TicklessStats *stats)
{
    taskENTER_CRITICAL();
    *stats = tickless_stats;
    taskEXIT_CRITICAL();
}

#if configUSE_TICKLESS_IDLE != 0
void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks)
{
    if (!tickless_stats.enabled || expected_idle_ticks < 2) {
        return;
    }
    if (expected_idle_ticks > MAX_IDLE_TICKS) {
        expected_idle_ticks = MAX_IDLE_TICKS;
    }

    /* The idle task has suspended the scheduler. Keep mie.MEIE enabled so WFI
     * can wake on pending interrupts, but mask mstatus.MIE until compensation
     * is complete. Do not use taskENTER_CRITICAL: the TCB nesting must remain
     * unchanged, and no pending PLIC/UART event may be cleared here. */
    unsigned int interrupt_state = core_interrupt_disable();
    __asm volatile("fence iorw, iorw" ::: "memory");
    if (eTaskConfirmSleepModeStatus() == eAbortSleep ||
        (read_csr(NDS_MIP) & read_csr(NDS_MIE)) != 0 ||
        (int32_t)(stimer_get_tick() - next_tick) >= 0) {
        tickless_stats.aborted_sleeps++;
        arm_tick_interrupt();
        core_restore_interrupt(interrupt_state);
        return;
    }

    /* Preserve the original tick phase, including the fraction already spent
     * in this tick. The last tick is left to the ISR to unblock delayed tasks. */
    uint32_t wake_tick = next_tick + (expected_idle_ticks - 1u) * TICK_CYCLES;
    arm_deadline(wake_tick);
    tickless_stats.sleep_entries++;
    __asm volatile("fence iorw, iorw\n\twfi\n\tfence iorw, iorw" ::: "memory");

    uint32_t now = stimer_get_tick();
    if (tick_pending()) {
        tickless_stats.timer_wakes++;
    }
    if (console_rx_pending()) {
        tickless_stats.uart_wakes++;
    } else if (!tick_pending()) {
        tickless_stats.other_wakes++;
    }

    TickType_t elapsed = 0;
    if ((int32_t)(now - next_tick) >= 0) {
        elapsed = (now - next_tick) / TICK_CYCLES + 1u;
    }
    tickless_stats.last_idle_ticks = elapsed;
    if (elapsed > tickless_stats.max_idle_ticks) {
        tickless_stats.max_idle_ticks = elapsed;
    }

    /* Never step across the next unblock time. In particular, do not let
     * vTaskStepTick take its equality branch (it enables task interrupts).
     * A due terminal tick or late wake is accounted by the normal ISR after
     * restoring MIE, while the scheduler is still suspended. */
    TickType_t stepped = elapsed;
    if (stepped >= expected_idle_ticks) {
        stepped = expected_idle_ticks - 1u;
    }
    if (stepped) {
        vTaskStepTick(stepped);
        next_tick += stepped * TICK_CYCLES;
        tickless_stats.skipped_ticks += stepped;
    }
    arm_tick_interrupt();
    core_restore_interrupt(interrupt_state);
}
#endif

BaseType_t xPortStartScheduler(void)
{
    portDISABLE_INTERRUPTS();
    configASSERT((xISRStackTop & portBYTE_ALIGNMENT_MASK) == 0);
    /* All RTOS-aware vectors use the assembly context wrapper. Nesting would
     * overwrite pxCurrentTCB->pxTopOfStack, so leave PLIC preemption disabled. */
    plic_preempt_feature_dis();
    plic_set_threshold(0);
    core_mie_disable(FLD_MIE_MTIE | FLD_MIE_MSIE);
    clear_tick();
    next_tick = stimer_get_tick() + TICK_CYCLES;
#ifdef TL3238_ENABLE_BLE
    timer_set_mode(TICK_TIMER, TIMER_MODE_SYSCLK);
    timer_set_irq_mask(TICK_MASK);
#else
    stimer_set_irq_mask(FLD_SYSTEM_IRQ_MASK);
#endif
    arm_tick_interrupt();
    plic_interrupt_enable(TICK_IRQ);
    core_mie_enable(FLD_MIE_MEIE);
    xPortStartFirstTask();
    return pdFAIL;
}

void vPortHandleInterrupt(unsigned int irq)
{
    tl3238_isr_active = pdTRUE;
    tl3238_isr_yield = pdFALSE;
    BaseType_t reschedule = pdFALSE;
    if (irq == TICK_IRQ) {
        tickless_stats.tick_irqs++;
        /* Advance from the previous deadline, including any ticks delayed by a
         * critical section. Signed subtraction handles the 32-bit STIMER wrap. */
        clear_tick();
        while ((int32_t)(stimer_get_tick() - next_tick) >= 0) {
            next_tick += TICK_CYCLES;
            reschedule |= xTaskIncrementTick();
        }
        arm_tick_interrupt();
    } else if (irq == IRQ_DMA) {
        reschedule = console_rx_isr();
#ifdef TL3238_ENABLE_BLE
    } else if (irq == IRQ_SYSTIMER || irq == IRQ_ZB_RT) {
        tl3238_controller_irq();
#ifdef TL3238_ENABLE_CS
    } else if (irq == IRQ_TIMER0) {
        tl3238_controller_cs_irq();
#endif
#endif
    }
    plic_interrupt_complete(irq);
    tl3238_isr_active = pdFALSE;
    if (reschedule || tl3238_isr_yield) {
        vTaskSwitchContext();
    }
}

void vPortEndScheduler(void)
{
    configASSERT(0);
    for (;;) { }
}

void vPortTaskExitError(void)
{
    configASSERT(0); /* Tasks must use vTaskDelete(NULL), never return. */
    for (;;) { }
}

/* Kept in RAM and visible through SWS even if serial output is unavailable. */
volatile unsigned long tl3238_trap_cause, tl3238_trap_pc, tl3238_trap_value;
void vPortTrap(unsigned long cause, unsigned long pc, unsigned long value)
{
    tl3238_trap_cause = cause;
    tl3238_trap_pc = pc;
    tl3238_trap_value = value;
    console_write("\r\nFATAL: CPU trap (inspect tl3238_trap_* via SWS)\r\n");
    for (;;) { }
}
