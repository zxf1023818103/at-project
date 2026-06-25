/*
 * tfm_storage_lock.c — NS-side flash/crypto arbitration around TF-M secure storage writes.
 *
 * WHY: a TF-M PS/ITS *write* (psa_*_set) makes the SPE program SPI flash via the ROM
 * FLASH_TxData* primitives. Those assume exclusive, auto-mode SPIC access. At runtime the
 * NSPE WiFi driver / LittleFS also drive the SPIC (under device_mutex_lock(RT_DEV_LOCK_FLASH)
 * + FLASH_Write_Lock) and the IPSEC HW-crypto engine (under RT_DEV_LOCK_CRYPTO). If the SPE
 * flash program runs while an NS flash/crypto op left the controller mid-operation, the ROM
 * reports "io driver parameters error" and the write fails (this blocked runtime `set key`).
 *
 * The SPE can't take these FreeRTOS mutexes itself (no RTOS in the secure world), and a
 * doorbell fired from inside the synchronous (SFN/L1) secure call can't either — the NS
 * scheduler is suspended for the duration, so a handler would be an ISR and device_mutex_lock
 * blocks. So we arbitrate on the NS side, in the calling task's context, AROUND the whole
 * synchronous PSA call: the blocking device_mutex_lock(RT_DEV_LOCK_FLASH) waits until any
 * in-progress NS flash op finishes (SPIC back in auto mode), then the SPE programs flash while
 * this task holds FLASH+CRYPTO, then we release. One acquire/release spans the entire set
 * (incl. PS's internal re-encrypt + filesystem compaction).
 *
 * Wired via the linker --wrap feature (app_km4 CMakeLists) so EVERY NS caller of psa_*_set /
 * psa_*_remove is covered — this app, the storage self-test, anything — with no edit to the
 * TF-M submodule's tfm_its_api.c / tfm_ps_api.c.
 *
 * SCOPE: this is the NS-task-side half. It serializes the SPE write against NS flash/crypto
 * *tasks*, but a KM4-only FreeRTOS mutex can't quiesce the *KM0* core, which keeps XIP-fetching
 * the shared SPI flash. Empirically that KM0 contention is what makes a >32 B runtime secure
 * write fail (ROM "io driver parameters error"); the SDK's FLASH_Write_Lock fixes it by
 * IPC-sleeping KM0, but that helper hangs when driven from this NS→secure path. So until the
 * KM0 quiesce is done at the SPE flash-program layer, runtime `set key` still fails and the demo
 * falls back to the embedded key. This wrapper is the correct NS-side complement for that fix.
 */
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"
#include "psa/internal_trusted_storage.h"
#include "psa/protected_storage.h"

/* osdep/device_lock.h — extern'd here to avoid pulling the osdep header tree into this TU. */
extern void device_mutex_lock(unsigned int device);
extern void device_mutex_unlock(unsigned int device);
#define DEV_LOCK_FLASH    1u   /* RT_DEV_LOCK_FLASH  */
#define DEV_LOCK_CRYPTO   2u   /* RT_DEV_LOCK_CRYPTO */

/* FLASH_Write_Lock IPC-sleeps KM0 (wfe) + masks KM4 irq/systick for the duration — the
 * CROSS-CORE quiesce device_mutex_lock (a KM4-only FreeRTOS mutex) can't do. It only works
 * now that KM0 registers the channel-2 FLASH_Write_IPC_Int handler (app_km0/main.c
 * ipc_table_init); before that it spun forever (KM0 never slept) and hung. The whole secure
 * PSA write (encrypt + flash program) then runs with KM0 quiesced, so the SPE's ROM flash
 * program no longer collides with KM0 XIP fetches on the shared SPI flash. */
extern void FLASH_Write_Lock(void);
extern void FLASH_Write_Unlock(void);

/* at-project: wfe-free KM0 XIP quiesce (rtl8721d_flash_ram.c). The vendored FLASH_Write_Lock above
 * was intentionally NOT used here: its KM0 `wfe` HardFaults when woken out of PMU deep-sleep, so
 * enabling it silently wedged (KM0 never set BIT_KM0_SLEEPSYS; post-mortem in
 * doc/amebad_km0_scheduler_and_runtime_flash.md). spe_km0_park instead spins KM0 in a RAM-resident
 * IPC handler (no wfe) and polls KM0's ack with a TIMEOUT, so it can never permanently wedge. This
 * is the missing cross-core piece: it parks KM0 across the SPE flash program (+ its block-GC) so
 * the wide page-programs don't collide with KM0 XIP and truncate the >1 KB key. */
extern void spe_km0_park(void);
extern void spe_km0_unpark(void);

/* Lock FLASH then CRYPTO (consistent order); unlock in reverse. Skip pre-scheduler: the only
 * caller then is the boot storage self-test, single-threaded with no WiFi/crypto contention,
 * and a FreeRTOS mutex can't block before vTaskStartScheduler(). `quiesce_km0` (set only for the
 * AWS key write) additionally parks KM0 across the secure flash program. */
static void hw_dev_lock(int quiesce_km0)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        device_mutex_lock(DEV_LOCK_FLASH);    /* serialize vs NS flash tasks (LFS/WiFi) */
        device_mutex_lock(DEV_LOCK_CRYPTO);   /* serialize vs NS IPSEC HW-crypto */
        if (quiesce_km0) {
            spe_km0_park();                   /* cross-core: park KM0 off the shared SPI flash */
        }
    }
}
static void hw_dev_unlock(int quiesce_km0)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        if (quiesce_km0) {
            spe_km0_unpark();
        }
        device_mutex_unlock(DEV_LOCK_CRYPTO);
        device_mutex_unlock(DEV_LOCK_FLASH);
    }
}

/* NS-interrupt mask flag in the TF-M NS dispatch (tfm_ns_interface_rtos.c): set across the secure
 * veneer call so no NS IRQ (SysTick, WiFi, ...) preempts the SPE while it programs the key into
 * flash. Scoped to ONLY the AWS private-key write (one-time, idle) - masking every ITS write would
 * stall the tick during the TLS handshake's entropy writes. The >1 KB key was the one corrupted by
 * the preemption (truncated to e.g. 96 B); the small entropy/seed writes were never affected. */
extern volatile uint32_t g_tfm_ns_mask_irq;
#define AWS_KEY_UID   ((psa_storage_uid_t)0x4157535F4B455901ULL)   /* "AWS_KEY\1" (== aws_provision.c) */

/* The real PSA implementations (tfm_its_api.c / tfm_ps_api.c), reached via --wrap. */
psa_status_t __real_psa_its_set(psa_storage_uid_t uid, size_t data_length,
                                const void *p_data, psa_storage_create_flags_t create_flags);
psa_status_t __real_psa_its_remove(psa_storage_uid_t uid);
psa_status_t __real_psa_ps_set(psa_storage_uid_t uid, size_t data_length,
                               const void *p_data, psa_storage_create_flags_t create_flags);
psa_status_t __real_psa_ps_remove(psa_storage_uid_t uid);

psa_status_t __wrap_psa_its_set(psa_storage_uid_t uid, size_t data_length,
                                const void *p_data, psa_storage_create_flags_t create_flags)
{
    psa_status_t s;
    int mask = (uid == AWS_KEY_UID);   /* mask NS IRQs + quiesce KM0 for the key write only */
    hw_dev_lock(mask);
    if (mask) { g_tfm_ns_mask_irq = 1; }
    s = __real_psa_its_set(uid, data_length, p_data, create_flags);
    if (mask) { g_tfm_ns_mask_irq = 0; }
    hw_dev_unlock(mask);
    return s;
}

psa_status_t __wrap_psa_its_remove(psa_storage_uid_t uid)
{
    psa_status_t s;
    hw_dev_lock(0);
    s = __real_psa_its_remove(uid);
    hw_dev_unlock(0);
    return s;
}

psa_status_t __wrap_psa_ps_set(psa_storage_uid_t uid, size_t data_length,
                               const void *p_data, psa_storage_create_flags_t create_flags)
{
    psa_status_t s;
    hw_dev_lock(0);
    s = __real_psa_ps_set(uid, data_length, p_data, create_flags);
    hw_dev_unlock(0);
    return s;
}

psa_status_t __wrap_psa_ps_remove(psa_storage_uid_t uid)
{
    psa_status_t s;
    hw_dev_lock(0);
    s = __real_psa_ps_remove(uid);
    hw_dev_unlock(0);
    return s;
}
