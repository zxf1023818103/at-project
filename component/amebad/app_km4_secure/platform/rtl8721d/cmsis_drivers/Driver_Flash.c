/*
 * RTL8721D KM4 — CMSIS Driver_FLASH0 for the TF-M secure world (ITS/PS/NV).
 *
 * Backs PSA Internal Trusted Storage / Protected Storage / NV counters with the
 * AmebaD SPI flash (4 MB @ 0x08000000). The TF-M flash FS hands us RAW SPI flash
 * offsets (e.g. 0x3F0000 = TFM_HAL_ITS_FLASH_AREA_ADDR), NOT XIP/0x08000000
 * aliases — the offsets the ROM FLASH_* primitives take.
 *
 * ROM access: like Driver_USART.c (LOGUART), we call the on-chip ROM flash
 * primitives by their fixed SECURE-alias addresses (ROM 0x10100000..0x1010A000,
 * the same ones the secure bootloader uses — see bootloader_km4/
 * rlx8721d_rom_symbol_acut_boot.ld). They are self-contained: each switches the
 * SPI controller to user mode, does the op, restores auto/XIP mode, and returns,
 * so XIP code fetches resume correctly after they return.
 *
 * XIP-write hazard: while a flash erase/program (or user-mode read) runs, the
 * flash device cannot serve XIP fetches. The ROM primitives run from ROM (not
 * XIP flash) so the SPE's own call path is safe; we additionally mask interrupts
 * around each op so no ISR vectors/handler fetches XIP from the busy device.
 *
 * CRITICAL — __DSB after each ROM erase/program: the ROM primitive switches the
 * SPIC to user mode, does the op, and restores auto/XIP mode, but it can RETURN
 * before those final SPIC-config writes have drained from the CPU write buffer.
 * At boot (single-threaded, idle SPIC) that latent write settles harmlessly, but
 * at runtime the next SPIC access (XIP fetch / the FS read-back) races the not-yet
 * -committed mode-restore and the buffered write surfaces as an IMPRECISE BusFault
 * (CFSR.IMPRECISERR) that escalates -> HardFault FORCED -> double-fault -> secure
 * LOCKUP (PC=0xEFFFFFFE). This was the real cause of the runtime `set key` /
 * psa_its_set hang on the erase/compaction path (a >32 B write or any replace),
 * mis-attributed earlier to "KM0 XIP contention" — confirmed by OpenOCD post-mortem
 * (KM0 was correctly parked in wfe; KM4 was locked up in the SPE). A __DSB() after
 * the ROM call forces the mode-restore to commit before anything else touches the
 * SPIC, which fixes it. (The SDK's FLASH_Write_Lock — IPC-sleep KM0 — is a separate
 * fetch-jitter mitigation, NOT this fix, and is an IMAGE2-RAM/NS function the SPE
 * can't reach anyway.) See doc/amebad_km0_scheduler_and_runtime_flash.md.
 */
#include "Driver_Flash.h"
#include "flash_layout.h"
#include "cmsis.h"          /* __disable_irq / __get_PRIMASK / __set_PRIMASK */

#define FLASH0_DRV_VERSION  ARM_DRIVER_VERSION_MAJOR_MINOR(1, 0)
#ifndef FLASH_AREA_IMAGE_SECTOR_SIZE
#define FLASH_AREA_IMAGE_SECTOR_SIZE (0x1000)
#endif

/* ---- AmebaD ROM flash primitives, called at their SECURE-alias addresses ----
 * (Thumb bit already set; addresses from bootloader_km4/rlx8721d_rom_symbol_acut_boot.ld,
 * which is the secure bootloader's ROM symbol table.) */
#define ROM_FLASH_ERASE_S       0x1010170dU   /* void FLASH_Erase(u32 type, u32 addr)            */
#define ROM_FLASH_TXDATA256B_S  0x1010151dU   /* void FLASH_TxData256B(u32 addr, u32 len, u8 *d) */

#define SPI_FLASH_BASE          0x08000000U   /* memory-mapped flash (auto/XIP read path) */
#define ERASE_SECTOR            2U            /* EraseSector (rtl8721d_flash.h) */
#define FLASH_PAGE_SIZE         256U

/* AN0400 §18.2: the SPIC-operating program/erase functions must execute from RAM, not XIP flash
 * — while the op holds the SPIC in user mode the device can't serve an XIP instruction fetch, so a
 * mid-op fetch (or one racing the auto-mode restore) corrupts/faults. RAMFUNC places them in the
 * .ramfunc section -> TF-M's CODE_RAM (region_defs.h S_RAM_CODE_*, copied to SRAM at boot). The
 * inline helpers are forced always_inline so they end up inside the RAM functions, not in flash. */
#define RAMFUNC          __attribute__((section(".ramfunc"), noinline, used))
#define ALWAYS_INLINE    static inline __attribute__((always_inline))

ALWAYS_INLINE void rom_flash_erase(uint32_t type, uint32_t addr)
{
    ((void (*)(uint32_t, uint32_t))ROM_FLASH_ERASE_S)(type, addr);
}
ALWAYS_INLINE void rom_flash_tx256(uint32_t addr, uint32_t len, uint8_t *data)
{
    ((void (*)(uint32_t, uint32_t, uint8_t *))ROM_FLASH_TXDATA256B_S)(addr, len, data);
}

/* Post-write cache maintenance — the missing half of what the SDK's flash path does via
 * Cache_Flush() after every program/erase. Two distinct hazards, both fatal at runtime:
 *  (1) __DSB drains the ROM primitive's SPIC user->auto mode-restore writes BEFORE the next
 *      SPIC access, else a buffered write surfaces as an imprecise BusFault -> secure LOCKUP.
 *  (2) Cache_Flush (SCB_InvalidateICache + SCB_InvalidateDCache): a program/erase leaves the
 *      XIP cache with stale lines for the NS code region AND stale D-cache lines for NS flash
 *      .rodata. A later NS XIP fetch reads a corrupted instruction, or a NS read of a
 *      flash-resident function pointer returns a corrupt value (an address bit flips, the valid
 *      0x0E... code alias becomes the unmapped 0x0C...) -> NS HardFault. This is exactly the
 *      Cache_Flush() the SDK's FLASH_*XIP wrappers do after every ROM program/erase
 *      (rtl8721d_flash_ram.c). The CMSIS SCB_Invalidate[ID]Cache() helpers aren't linkable in
 *      the SPE build, so the canonical set/way (DCISW) + ICIALLU sequences are inlined via raw
 *      SCB registers. The KM4 D-cache only covers (read-only) flash, so the invalidate has no
 *      dirty SRAM lines to lose. */
#define SCB_CSSELR   (*(volatile uint32_t *)0xE000ED84U)   /* cache size selection                 */
#define SCB_CCSIDR   (*(volatile uint32_t *)0xE000ED80U)   /* cache size id (sets/ways)            */
#define SCB_ICIALLU  (*(volatile uint32_t *)0xE000EF50U)   /* I-cache invalidate all               */
#define SCB_DCCISW   (*(volatile uint32_t *)0xE000EF74U)   /* D-cache CLEAN+invalidate by set/way  */

ALWAYS_INLINE void flash_write_barrier(void)
{
    uint32_t ccsidr, sets, ways;

    __DSB();

    /* D-cache clean+invalidate all by set/way (canonical CMSIS sequence; SET_Pos=5, WAY_Pos=30).
     * CLEAN+invalidate (DCCISW), NOT plain invalidate (DCISW): this KM4 caches SRAM too, so the
     * SPE's own dirty stack/data lines must be written back before being dropped, or the SPE
     * corrupts itself (a plain invalidate-all crashed the boot self-test write). */
    SCB_CSSELR = 0U;
    __DSB();
    ccsidr = SCB_CCSIDR;
    sets = (ccsidr >> 13) & 0x7FFFU;          /* CCSIDR.NumSets        (bits 27:13) */
    do {
        ways = (ccsidr >> 3) & 0x3FFU;        /* CCSIDR.Associativity  (bits 12:3)  */
        do {
            SCB_DCCISW = ((sets & 0x1FFU) << 5) | ((ways & 0x3U) << 30);
        } while (ways-- != 0U);
    } while (sets-- != 0U);

    /* I-cache invalidate all. */
    SCB_ICIALLU = 0U;

    __DSB();
    __ISB();
}

static const ARM_DRIVER_VERSION DriverVersion = {
    ARM_FLASH_API_VERSION, FLASH0_DRV_VERSION
};
static const ARM_FLASH_CAPABILITIES DriverCapabilities = {
    0, /* event_ready */
    0, /* data_width: 8-bit */
    1, /* erase_chip */
    0
};
static ARM_FLASH_INFO FlashInfo = {
    NULL,
    FLASH_TOTAL_SIZE / FLASH_AREA_IMAGE_SECTOR_SIZE, /* sector_count */
    FLASH_AREA_IMAGE_SECTOR_SIZE,                    /* sector_size  */
    1,                                               /* page_size    */
    1,                                               /* program_unit */
    0xFF,                                            /* erased_value */
};

static ARM_DRIVER_VERSION ARM_Flash_GetVersion(void)        { return DriverVersion; }
static ARM_FLASH_CAPABILITIES ARM_Flash_GetCapabilities(void) { return DriverCapabilities; }
static int32_t ARM_Flash_Initialize(ARM_Flash_SignalEvent_t cb) { (void)cb; return ARM_DRIVER_OK; }
static int32_t ARM_Flash_Uninitialize(void)                 { return ARM_DRIVER_OK; }
static int32_t ARM_Flash_PowerControl(ARM_POWER_STATE s)    { (void)s; return ARM_DRIVER_OK; }

static int32_t ARM_Flash_ReadData(uint32_t addr, void *data, uint32_t cnt)
{
    /* Memory-mapped read via the flash auto/XIP window — the SAME SPIC path the
     * SPE already executes its own code from, so it reconfigures nothing and can't
     * disturb the secure XIP mapping (unlike a user-mode FLASH_RxData, which
     * toggles the SPIC out of auto mode and hung the SPE at ITS-FS init). The
     * program/erase paths below DCache_Invalidate the region so these reads stay
     * coherent after a write. */
    const volatile uint8_t *src = (const volatile uint8_t *)(SPI_FLASH_BASE + addr);
    uint8_t *dst = (uint8_t *)data;
    uint32_t i;
    for (i = 0; i < cnt; i++) {
        dst[i] = src[i];
    }
    return (int32_t)cnt;
}

RAMFUNC static int32_t ARM_Flash_ProgramData(uint32_t addr, const void *data, uint32_t cnt)
{
    /* Bounce buffer for word-aligning a chunk (see below). One page; in SRAM (.bss). */
    static uint8_t pad_buf[FLASH_PAGE_SIZE];
    const uint8_t *p = (const uint8_t *)data;
    uint32_t remaining = cnt;
    uint32_t primask;

    while (remaining > 0U) {
        /* A page-program must not cross a 256-byte page boundary. */
        uint32_t page_room = FLASH_PAGE_SIZE - (addr & (FLASH_PAGE_SIZE - 1U));
        uint32_t chunk = (remaining < page_room) ? remaining : page_room;
        const uint8_t *src = p;
        uint32_t prog = chunk;

        /* The ROM FLASH_TxData256B asserts (io_assert_failed) on a non-word-aligned length — the
         * SPIC programs in 32-bit words. The ITS flash FS hands us odd lengths (e.g. a 1193-byte
         * RSA-2048 key DER), so round the program length UP to a multiple of 4, padding the tail
         * with 0xFF (the erased value, a no-op on the cells). Done via a bounce buffer so we never
         * read past the caller's source. The padding stays inside the current page (the odd chunk
         * is the file's last, partial one, so there's room); the FS reads back only the real length
         * from its metadata, so the pad bytes are invisible. */
        if ((chunk & 3U) != 0U) {
            uint32_t up = (chunk + 3U) & ~3U;
            if (up <= page_room) {
                uint32_t i;
                for (i = 0; i < chunk; i++) pad_buf[i] = p[i];
                for (; i < up; i++)         pad_buf[i] = 0xFFU;
                src  = pad_buf;
                prog = up;
            }
        }

        primask = __get_PRIMASK();
        __disable_irq();
        rom_flash_tx256(addr, prog, (uint8_t *)(uintptr_t)src);
        flash_write_barrier();
        __set_PRIMASK(primask);

        addr      += chunk;
        p         += chunk;
        remaining -= chunk;
    }
    return (int32_t)cnt;
}

RAMFUNC static int32_t ARM_Flash_EraseSector(uint32_t addr)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    rom_flash_erase(ERASE_SECTOR, addr);
    flash_write_barrier();
    __set_PRIMASK(primask);
    return ARM_DRIVER_OK;
}

static int32_t ARM_Flash_EraseChip(void)                    { return ARM_DRIVER_ERROR_UNSUPPORTED; }
static ARM_FLASH_STATUS ARM_Flash_GetStatus(void)           { ARM_FLASH_STATUS s = {0}; return s; }
static ARM_FLASH_INFO * ARM_Flash_GetInfo(void)             { return &FlashInfo; }

ARM_DRIVER_FLASH Driver_FLASH0 = {
    ARM_Flash_GetVersion,
    ARM_Flash_GetCapabilities,
    ARM_Flash_Initialize,
    ARM_Flash_Uninitialize,
    ARM_Flash_PowerControl,
    ARM_Flash_ReadData,
    ARM_Flash_ProgramData,
    ARM_Flash_EraseSector,
    ARM_Flash_EraseChip,
    ARM_Flash_GetStatus,
    ARM_Flash_GetInfo
};
