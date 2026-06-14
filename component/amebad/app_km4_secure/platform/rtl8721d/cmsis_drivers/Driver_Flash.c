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
 * (The SDK's FLASH_Write_Lock additionally IPC-sleeps KM0 to avoid fetch jitter;
 * that helper is an IMAGE2-RAM/NS function and is not reachable from the SPE. For
 * ITS/PS — small, infrequent secure writes — masking interrupts is sufficient;
 * a brief KM0 fetch stall during an erase is tolerated, not a fault.)
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

static inline void rom_flash_erase(uint32_t type, uint32_t addr)
{
    ((void (*)(uint32_t, uint32_t))ROM_FLASH_ERASE_S)(type, addr);
}
static inline void rom_flash_tx256(uint32_t addr, uint32_t len, uint8_t *data)
{
    ((void (*)(uint32_t, uint32_t, uint8_t *))ROM_FLASH_TXDATA256B_S)(addr, len, data);
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

static int32_t ARM_Flash_ProgramData(uint32_t addr, const void *data, uint32_t cnt)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t remaining = cnt;
    uint32_t primask;

    while (remaining > 0U) {
        /* A page-program must not cross a 256-byte page boundary. */
        uint32_t page_room = FLASH_PAGE_SIZE - (addr & (FLASH_PAGE_SIZE - 1U));
        uint32_t chunk = (remaining < page_room) ? remaining : page_room;

        primask = __get_PRIMASK();
        __disable_irq();
        rom_flash_tx256(addr, chunk, (uint8_t *)(uintptr_t)p);
        __set_PRIMASK(primask);

        addr      += chunk;
        p         += chunk;
        remaining -= chunk;
    }
    return (int32_t)cnt;
}

static int32_t ARM_Flash_EraseSector(uint32_t addr)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    rom_flash_erase(ERASE_SECTOR, addr);
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
