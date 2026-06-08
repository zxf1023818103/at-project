/*
 * RTL8721D KM4 — minimal CMSIS Driver_FLASH0 for TF-M ITS/PS + NV counters.
 * Bring-up stub: links the TF-M flash backend. A real driver over the Ameba
 * SPI-flash (FLASH_BASE 0x08000000) is a follow-on; until then ITS/PS/NV are
 * non-functional (reads return erased, writes are no-ops).
 */
#include "Driver_Flash.h"
#include "flash_layout.h"

#define FLASH0_DRV_VERSION  ARM_DRIVER_VERSION_MAJOR_MINOR(1, 0)
#ifndef FLASH_AREA_IMAGE_SECTOR_SIZE
#define FLASH_AREA_IMAGE_SECTOR_SIZE (0x1000)
#endif

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
    /* Stub: return erased pattern. */
    for (uint32_t i = 0; i < cnt; i++) { ((uint8_t *)data)[i] = 0xFF; }
    (void)addr;
    return (int32_t)cnt;
}
static int32_t ARM_Flash_ProgramData(uint32_t a, const void *d, uint32_t c) { (void)a; (void)d; return (int32_t)c; }
static int32_t ARM_Flash_EraseSector(uint32_t addr)         { (void)addr; return ARM_DRIVER_OK; }
static int32_t ARM_Flash_EraseChip(void)                    { return ARM_DRIVER_OK; }
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
