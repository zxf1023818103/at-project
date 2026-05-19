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

#else  // non-Ameba D platforms: no-op stubs

namespace at {

void lockFirmwareFlash() {}

void unlockFirmwareFlash() {}

}  // namespace at

#endif
