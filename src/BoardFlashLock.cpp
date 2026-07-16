#include "BoardFlashLock.h"

#if defined __has_include
#  if __has_include("platform_autoconf.h")
#    include "platform_autoconf.h"
#  endif
#endif

#if defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C || \
    defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2

#include <cstdint>
#include <cstdio>

#if defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
// On amebagreen2, flash_api.h transitively pulls in PeripheralNames.h whose
// mbed enum initializers like `UART_0 = (int)UART0_DEV` aren't valid C++
// constant expressions. Pre-define the guard to skip its body — we only
// need FLASH_InitTypeDef from objects.h, not the mbed UART/SPI/I2C enums.
#define MBED_PERIPHERALNAMES_H
extern "C" {
#include "ameba_soc.h"  // FLASH_RxCmd / FLASH_SetStatus / flash_init_para / FLASH_CMD_RDSR2
#include "flash_api.h"  // flash_t / flash_get_status / flash_set_status
}
#else
extern "C" {
#include "flash_api.h"
#include "rtl8721d_flash.h"
}
#endif

namespace at {

namespace {

// SR1 / SR2 bit layout is the W25Q32JV / GD25Q32 standard on the Ameba SKUs we
// drive: SR1=0x14 (TB=0, BP[2:0]=0b101) + SR2 |= 0x40 (CMP=1) ⇒ the indicated
// "top 1 MB" becomes the UNprotected region, so the bottom 3 MB
// (0x000000–0x2FFFFF) stays write-locked. MXIC-style chips need a different
// command set (covered only by the newer SDK's runtime auto-detect below).
constexpr uint8_t kSr1Mask         = 0xFC;   // writable SR1 bits we drive (SRP0|SEC|TB|BP[2:0])
constexpr uint8_t kSr1Locked       = 0x14;
constexpr uint8_t kSr2CmpBit       = 0x40;

uint8_t readStatus2() {
    // Every Ameba SDK declares flash_get_status2() in flash_api.h but never
    // implements it; use the raw RX-cmd path (0x35 = chip-standard Read SR2;
    // the rtl8721d SDK has no FLASH_CMD_RDSR2 enum).
#if defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
    constexpr uint8_t cmd = FLASH_CMD_RDSR2;
#else
    constexpr uint8_t cmd = 0x35;
#endif
    uint8_t v = 0;
    FLASH_RxCmd(cmd, 1, &v);
    return v;
}

uint8_t writeStatus2Cmd() {
#if defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
    // Runtime value from the SDK's flash auto-detect handles W25Q/GD25/MXIC
    // correctly. Fall back to W25Q-standard 0x31 if unset.
    uint8_t cmd = flash_init_para.FLASH_cmd_wr_status2;
    return cmd != 0 ? cmd : 0x31;
#else
    return 0x31;   // W25Q/GD25-standard Write SR2 (the 8721D SDK has no auto-detect field)
#endif
}

}  // namespace

bool lockFirmwareFlash() {
    flash_t flash;
    int sr1 = flash_get_status(&flash);
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 | kSr2CmpBit);

    if (((sr1 & kSr1Mask) == kSr1Locked) && ((sr2 & kSr2CmpBit) != 0)) {
        return true;  // already locked
    }
    flash_set_status(&flash, kSr1Locked);
    const uint8_t cmd = writeStatus2Cmd();
    FLASH_SetStatus(cmd, 1, &sr2_desired);
    const uint8_t verify_sr2 = readStatus2();
    if ((static_cast<uint8_t>(flash_get_status(&flash)) & kSr1Mask) != kSr1Locked ||
        (verify_sr2 & kSr2CmpBit) == 0) {
        printf("[flash] firmware lock VERIFY FAILED: SR1 want=0x%02x got=0x%02x SR2 want=0x%02x got=0x%02x (wr_cmd=0x%02x)\r\n",
               kSr1Locked, static_cast<unsigned>(flash_get_status(&flash)) & kSr1Mask,
               sr2_desired, verify_sr2, cmd);
        return false;
    }
    printf("[flash] firmware locked: SR1=0x%02x SR2=0x%02x (wr_cmd=0x%02x)\r\n",
           kSr1Locked, verify_sr2, cmd);
    return true;
}

bool unlockFirmwareFlash() {
    flash_t flash;
    uint8_t sr2 = readStatus2();
    uint8_t sr2_desired = static_cast<uint8_t>(sr2 & ~kSr2CmpBit);
    flash_set_status(&flash, 0x00);
    const uint8_t cmd = writeStatus2Cmd();
    FLASH_SetStatus(cmd, 1, &sr2_desired);
    const uint8_t verify_sr2 = readStatus2();
    if ((verify_sr2 & kSr2CmpBit) != 0) {
        printf("[flash] firmware unlock VERIFY FAILED: SR2 want=0x%02x got=0x%02x (wr_cmd=0x%02x)\r\n",
               sr2_desired, verify_sr2, cmd);
        return false;
    }
    return true;
}

}  // namespace at

#elif defined AT_UART_BACKEND_BFLB

// The Bouffalo SR1/SR2 write mechanics moved to the cross-platform flash write-protect
// (component/bouffalo_sdk/board_flash_port, board_flash_protect). Keep the at:: C++ helper as a thin
// wrapper so both it and the shared `flash` command drive one implementation.
extern "C" {
#include "board_flash_lock.h"
}

namespace at {

bool lockFirmwareFlash() {
    return board_flash_protect(BOARD_FLASH_LOCK_FW, nullptr, nullptr, nullptr, nullptr) != 0;
}

bool unlockFirmwareFlash() {
    return board_flash_protect(BOARD_FLASH_UNLOCK, nullptr, nullptr, nullptr, nullptr) != 0;
}

}  // namespace at

#else  // other platforms: no-op stubs

namespace at {

bool lockFirmwareFlash() { return true; }

bool unlockFirmwareFlash() { return true; }

}  // namespace at

#endif
