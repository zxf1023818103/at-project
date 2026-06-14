/*
 * efuse_huk.h — read the AmebaD RTL8721D TrustZone-secure user eFuse from the
 * secure world (TF-M SPE). Used both by the Platform partition's eFuse-info IOCTL
 * (services/src/tfm_platform_system.c) and by the crypto-keys HAL's HUK loader
 * (services/src/crypto_keys.c).
 *
 * The 32-byte user-usable TrustZone-secure eFuse (0x150..0x16F; 0x170 is the RDP
 * key) is the device root key (HUK). Reading mirrors the SDK's img3
 * Secure_ReadEfuse: CtrlSetting = *(SYSTEM_CTRL_BASE + REG_HS_EFUSE_CTRL1_S), then
 * the ROM EFUSERead8 per byte. Constants hard-coded (like Driver_Flash's ROM
 * aliases) since the SPE links no AmebaD fwlib:
 *   EFUSERead8 secure ROM   = 0x10100a31  u32(u32 ctrl, u32 addr, u8 *d, u8 vol)
 *   REG_HS_EFUSE_CTRL1_S    = SYSTEM_CTRL_BASE 0x40000000 + 0x810 = 0x40000810
 *   L25EOUTVOLTAGE          = 7
 * (SYSTEM_CTRL_BASE/offset verified against the secure bootloader's RDP-key read.)
 */
#ifndef AMEBAD_EFUSE_HUK_H
#define AMEBAD_EFUSE_HUK_H

#include <stdint.h>

#define AMEBAD_EFUSE_HUK_ADDR   0x150u   /* TrustZone-secure user eFuse base */
#define AMEBAD_EFUSE_HUK_SIZE   32u      /* 0x150..0x16F (0x170 = RDP key)    */

#define AMEBAD_ROM_EFUSEREAD8_S 0x10100a31u
#define AMEBAD_EFUSE_CTRL1_S    0x40000810u
#define AMEBAD_EFUSE_L25_VOL    7u

static inline void amebad_secure_read_efuse(uint32_t addr, uint32_t size, uint8_t *buf)
{
    uint32_t ctrl = *(volatile uint32_t *)AMEBAD_EFUSE_CTRL1_S;
    uint32_t i;

    for (i = 0; i < size; i++) {
        ((uint32_t (*)(uint32_t, uint32_t, uint8_t *, uint8_t))AMEBAD_ROM_EFUSEREAD8_S)
            (ctrl, addr + i, &buf[i], (uint8_t)AMEBAD_EFUSE_L25_VOL);
    }
}

#endif /* AMEBAD_EFUSE_HUK_H */
