#include "BoardFlashLock.h"

#if defined __has_include
#  if __has_include("platform_autoconf.h")
#    include "platform_autoconf.h"
#  endif
#endif

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C

#include <cstdint>
#include <cstdio>

extern "C" {
#include "flash_api.h"
#include "rtl8721d_flash.h"
}

namespace at {

namespace {

// W25Q32JV / GD25Q32 standard: 0x35 = Read SR2, 0x31 = Write SR2. Common
// 4 MB SiP NORs on Ameba D SKUs follow this layout; MXIC-style chips need
// a different command set (not handled here).
constexpr uint8_t kCmdReadStatus2  = 0x35;
constexpr uint8_t kCmdWriteStatus2 = 0x31;

// SR1 bits we drive: TB=0 (bit 5), BP[2:0]=0b101 (bits 4..2) ⇒ 0x14
// Mask of writable SR1 bits we care about (SRP0|SEC|TB|BP[2:0]).
constexpr uint8_t kSr1Mask         = 0xFC;
constexpr uint8_t kSr1Locked       = 0x14;

// SR2: CMP at bit 6. With CMP=1 + BP=1 MB + TB=0, the indicated "top 1 MB"
// becomes the UNprotected region, so the bottom 3 MB (0x000000–0x2FFFFF)
// stays write-locked.
constexpr uint8_t kSr2CmpBit       = 0x40;

uint8_t readStatus2() {
    // The SDK declares flash_get_status2() in flash_api.h but never
    // implements it on Ameba D. Use the raw RX-cmd path instead.
    uint8_t v = 0;
    FLASH_RxCmd(kCmdReadStatus2, 1, &v);
    return v;
}

}  // namespace

void lockFirmwareFlash() {
    flash_t flash;
    int sr1 = flash_get_status(&flash);
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 | kSr2CmpBit);

    if (((sr1 & kSr1Mask) == kSr1Locked) && ((sr2 & kSr2CmpBit) != 0)) {
        return;  // already locked
    }
    flash_set_status(&flash, kSr1Locked);
    FLASH_SetStatus(kCmdWriteStatus2, 1, &sr2_desired);
    printf("[flash] firmware locked: SR1=0x%02x SR2=0x%02x\r\n",
           kSr1Locked, sr2_desired);
}

void unlockFirmwareFlash() {
    flash_t flash;
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 & ~kSr2CmpBit);
    flash_set_status(&flash, 0x00);
    FLASH_SetStatus(kCmdWriteStatus2, 1, &sr2_desired);
}

}  // namespace at

#elif defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#include <cstdint>
#include <cstdio>

// On amebagreen2, flash_api.h transitively pulls in PeripheralNames.h whose
// mbed enum initializers like `UART_0 = (int)UART0_DEV` aren't valid C++
// constant expressions. Pre-define the guard to skip its body — we only
// need FLASH_InitTypeDef from objects.h, not the mbed UART/SPI/I2C enums.
#define MBED_PERIPHERALNAMES_H

extern "C" {
#include "ameba_soc.h"  // FLASH_RxCmd / FLASH_SetStatus / flash_init_para / FLASH_CMD_RDSR2
#include "flash_api.h"  // flash_t / flash_get_status / flash_set_status
}

namespace at {

namespace {

// SR1 / SR2 bit layout is identical to W25Q / GD25 family on Ameba D. Locking
// values are the same: SR1=0x14 (TB=0, BP[2:0]=101) + SR2 |= 0x40 (CMP=1)
// ⇒ "top 1 MB unprotected, bottom 3 MB write-locked".
constexpr uint8_t kSr1Mask         = 0xFC;
constexpr uint8_t kSr1Locked       = 0x14;
constexpr uint8_t kSr2CmpBit       = 0x40;

uint8_t readStatus2() {
    // flash_get_status2 is declared in flash_api.h but unimplemented in the
    // new SDK as well; go raw with the chip-standard Read SR2 command.
    uint8_t v = 0;
    FLASH_RxCmd(FLASH_CMD_RDSR2, 1, &v);
    return v;
}

uint8_t writeStatus2Cmd() {
    // Runtime value from the SDK's flash auto-detect handles W25Q/GD25/MXIC
    // correctly. Fall back to W25Q-standard 0x31 if unset.
    uint8_t cmd = flash_init_para.FLASH_cmd_wr_status2;
    return cmd != 0 ? cmd : 0x31;
}

}  // namespace

void lockFirmwareFlash() {
    flash_t flash;
    int sr1 = flash_get_status(&flash);
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 | kSr2CmpBit);

    if (((sr1 & kSr1Mask) == kSr1Locked) && ((sr2 & kSr2CmpBit) != 0)) {
        return;  // already locked
    }
    flash_set_status(&flash, kSr1Locked);
    uint8_t cmd = writeStatus2Cmd();
    FLASH_SetStatus(cmd, 1, &sr2_desired);
    printf("[flash] firmware locked: SR1=0x%02x SR2=0x%02x (wr_cmd=0x%02x)\r\n",
           kSr1Locked, sr2_desired, cmd);
}

void unlockFirmwareFlash() {
    flash_t flash;
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 & ~kSr2CmpBit);
    flash_set_status(&flash, 0x00);
    uint8_t cmd = writeStatus2Cmd();
    FLASH_SetStatus(cmd, 1, &sr2_desired);
}

}  // namespace at

#elif defined AT_UART_BACKEND_BFLB

#include <cstdint>
#include <cstdio>

extern "C" {
// We only need spi_flash_cfg_type as opaque pointer here. Forward-declare
// to avoid pulling in chip-specific headers (BL602/BL616/BL616CL each have
// their own copy of the full struct definition).
struct spi_flash_cfg_type;

// LHAL flash APIs, signatures from drivers/lhal/include/bflb_flash.h and
// drivers/lhal/src/flash/bflb_sflash.h. reg_index 0 → SR1, 1 → SR2 per the
// flash_cfg->{read,write}_reg_cmd[] table populated by SDK init.
void bflb_flash_get_cfg(uint8_t **cfg_addr, uint32_t *len);
int bflb_sflash_read_reg(struct spi_flash_cfg_type *cfg, uint8_t reg_index,
                         uint8_t *reg_value, uint8_t reg_len);
int bflb_sflash_write_reg(struct spi_flash_cfg_type *cfg, uint8_t reg_index,
                          uint8_t *reg_value, uint8_t reg_len);
}

namespace at {

namespace {

// SR1 / SR2 bit layout matches Ameba — same W25Q / GD25 family on Bouffalo
// 4 MB SiP NORs. Lock values: SR1=0x14 (TB=0, BP[2:0]=101) + SR2 |= 0x40
// (CMP=1) ⇒ "top 1 MB unprotected, bottom 3 MB write-locked".
constexpr uint8_t kSr1Mask    = 0xFC;
constexpr uint8_t kSr1Locked  = 0x14;
constexpr uint8_t kSr2CmpBit  = 0x40;

struct spi_flash_cfg_type *getFlashCfg() {
    uint8_t *cfg = nullptr;
    uint32_t len = 0;
    bflb_flash_get_cfg(&cfg, &len);
    return reinterpret_cast<struct spi_flash_cfg_type *>(cfg);
}

}  // namespace

void lockFirmwareFlash() {
    auto *cfg = getFlashCfg();
    if (cfg == nullptr) return;

    uint8_t sr1 = 0;
    uint8_t sr2 = 0;
    bflb_sflash_read_reg(cfg, 0, &sr1, 1);
    bflb_sflash_read_reg(cfg, 1, &sr2, 1);

    if (((sr1 & kSr1Mask) == kSr1Locked) && ((sr2 & kSr2CmpBit) != 0)) {
        return;  // already locked
    }

    // Write SR1+SR2 atomically via the 0x01 Write-Status command (low byte =
    // new SR1, high byte = new SR2). Pattern lifted from
    // drivers/soc/bl602/std/src/bl602_sflash_ext.c:596.
    uint16_t combined = static_cast<uint16_t>(kSr1Locked) |
                        (static_cast<uint16_t>(sr2 | kSr2CmpBit) << 8);
    bflb_sflash_write_reg(cfg, 0, reinterpret_cast<uint8_t *>(&combined), 2);

    printf("[flash] firmware locked: SR1=0x%02x SR2=0x%02x\r\n",
           static_cast<unsigned>(combined & 0xFF),
           static_cast<unsigned>((combined >> 8) & 0xFF));
}

void unlockFirmwareFlash() {
    auto *cfg = getFlashCfg();
    if (cfg == nullptr) return;

    uint8_t sr2 = 0;
    bflb_sflash_read_reg(cfg, 1, &sr2, 1);
    uint16_t combined = 0x0000u |
                        (static_cast<uint16_t>(sr2 & ~kSr2CmpBit) << 8);
    bflb_sflash_write_reg(cfg, 0, reinterpret_cast<uint8_t *>(&combined), 2);
}

}  // namespace at

#else  // other platforms: no-op stubs

namespace at {

void lockFirmwareFlash() {}

void unlockFirmwareFlash() {}

}  // namespace at

#endif
