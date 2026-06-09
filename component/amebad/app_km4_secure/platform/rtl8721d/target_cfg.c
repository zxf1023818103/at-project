/*
 * RTL8721D (Ameba-D) KM4 — TF-M target_cfg: TrustZone / IDAU / SAU / NVIC.
 *
 * This is the heart of the port and the fix for the secure-attribution faults:
 * it programs the Ameba-D KM4 IDAU (KM4_IDAU_BASE 0x5000C100) + the Armv8-M SAU
 * so the Non-secure world (the driver-only app / QUIC NSPE) is correctly
 * attributed, while the TF-M SPE regions stay Secure.
 *
 * The IDAU/SAU/NVIC programming mirrors the SDK's secure bootloader
 * (component/amebad/bootloader_km4/boot_trustzone_hp.c :: BOOT_RAM_TZCfg) — the
 * authoritative RTL8721D recipe — but the region table below reflects THIS port's
 * partition (region_defs.h) instead of the SDK's IMG3/RDP layout:
 *
 *   Secure (omitted from the NS table -> Secure by SAU default-when-enabled):
 *     S_CODE  0x10050000..0x1005FFFF  (TF-M SPE, RAM-resident) [NSC carved @0x10050400]
 *     S_DATA  0x10060000..0x1007FFFF  (TF-M SPE RAM)
 *   Non-secure (explicit entries):
 *     low      0x00000000..0x0DFFFFFF (KM0 SRAM, retention, PSRAM, SPI flash)
 *     NS code  0x0E040000..0x0FFFFFFF (NS app XIP)
 *     NS RAM   0x10000000..0x1004FFFF (NS app SRAM, below S_CODE)
 *     ext SRAM 0x100E0000..0x100FFFFF (BT/WiFi extension SRAM)
 *     ROM NS   0x1010A000..0x101D3FFF (IROM_NS / DROM_NS)
 *     periph   0x40000000..0x4FFFFFFF (NS peripheral alias; Secure alias 0x5xxx)
 *   NSC:
 *     veneers  top CMSE_VENEER_REGION_SIZE of S_CODE
 *
 * NB (bring-up): SAU has only 8 regions; the table is sized to fit. PPC
 * (peripheral protection, via rtl8721d_trustzone.h TZ_ConfigSlaveSecurity) is a
 * follow-on for isolation L2/L3. Register/type deps (SAU_*, SCB, NVIC, CMSIS)
 * come from the Ameba-D CMSIS device headers wired up by the platform CMakeLists.
 */
#include "target_cfg.h"
#include "region_defs.h"
#include "rtl8721d_device.h"   /* CMSIS core_cm33 (SAU/SCB/NVIC) + Ameba-D device */

/* Ameba-D KM4 IDAU. */
#ifndef KM4_IDAU_BASE
#define KM4_IDAU_BASE   0x5000C100
#endif
typedef struct { volatile uint32_t BARx; volatile uint32_t LARx; } idau_entry_t;
typedef struct { idau_entry_t ENTRY[8]; volatile uint32_t CTRL; volatile uint32_t LOCK; } idau_regs_t;
#define AMEBA_IDAU      ((idau_regs_t *)KM4_IDAU_BASE)

/* NS / NSC region table for this port's partition (Start, End inclusive, NSC). */
typedef struct { uint32_t start; uint32_t end; uint32_t nsc; } tz_region_t;

static const tz_region_t s_ns_regions[] = {
    { 0x00000000u, 0x0DFFFFFFu, 0 },                  /* low NS: KM0/retention/PSRAM + flash (incl. tfm_s.bin @0x08100000, but it runs cached @S_CODE) */
    { 0x0E000000u, (S_CODE_START - 1u), 0 },          /* XIP NS below tfm_s (image2/app_km4 cached XIP) */
    { (S_CODE_START + S_CODE_SIZE), 0x0FFFFFFFu, 0 }, /* XIP NS above tfm_s window */
    { 0x10000000u, (S_DATA_START - 1u), 0 },          /* NS SRAM below S_DATA (S_DATA = secure, omitted) */
    { 0x100E0000u, 0x100FFFFFu, 0 },                  /* BT/WiFi extension SRAM         */
    { 0x1010A000u, 0x101D3FFFu, 0 },                  /* IROM_NS / DROM_NS              */
    { 0x40000000u, 0x4FFFFFFFu, 0 },                  /* NS peripheral alias            */
    { CMSE_VENEER_REGION_START,                       /* NSC veneers (in S_CODE cached-XIP window) */
      (CMSE_VENEER_REGION_START + CMSE_VENEER_REGION_SIZE - 1u), 1 },
};
#define NUM_TZ_REGIONS  (sizeof(s_ns_regions) / sizeof(s_ns_regions[0]))

void enable_fault_handlers(void)
{
    /* Enable SecureFault, BusFault, UsageFault, MemManage. */
    SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk
                | SCB_SHCSR_BUSFAULTENA_Msk
                | SCB_SHCSR_USGFAULTENA_Msk
                | SCB_SHCSR_MEMFAULTENA_Msk;
}

void system_reset_cfg(void)
{
    uint32_t reg = SCB->AIRCR & ~(SCB_AIRCR_VECTKEY_Msk
                                  | SCB_AIRCR_PRIS_Msk
                                  | SCB_AIRCR_BFHFNMINS_Msk);
    /* PRIS=1 (NS prio lowered), BFHFNMINS=1 (Bus/Hard/NMI -> NS) — per BOOT_RAM_TZCfg. */
    SCB->AIRCR = reg | (0x05FAu << SCB_AIRCR_VECTKEY_Pos)
                     | SCB_AIRCR_PRIS_Msk
                     | SCB_AIRCR_BFHFNMINS_Msk;
}

void sau_and_idau_cfg(void)
{
    idau_regs_t *idau = AMEBA_IDAU;
    uint32_t i;

    /* IDAU: program the NSC==0 (plain NS) regions. (NSC regions are handled by
     * the SAU side via the NSC bit.) Mirrors BOOT_RAM_TZCfg. */
    for (i = 0; i < NUM_TZ_REGIONS && i < 8u; i++) {
        if (s_ns_regions[i].nsc == 0u) {
            idau->ENTRY[i].BARx = s_ns_regions[i].start;
            idau->ENTRY[i].LARx = s_ns_regions[i].end;
            idau->CTRL |= (1u << i);
        }
    }
    idau->LOCK = 1u;

    /* SAU: one region per table entry; default (no match) = Secure since CTRL.ALLNS=0. */
    for (i = 0; i < NUM_TZ_REGIONS && i < 8u; i++) {
        SAU->RNR  = i;
        SAU->RBAR = (s_ns_regions[i].start & SAU_RBAR_BADDR_Msk);
        SAU->RLAR = (s_ns_regions[i].end & SAU_RLAR_LADDR_Msk)
                  | ((s_ns_regions[i].nsc << SAU_RLAR_NSC_Pos) & SAU_RLAR_NSC_Msk)
                  | SAU_RLAR_ENABLE_Msk;
    }
    SAU->CTRL = SAU_CTRL_ENABLE_Msk;   /* enable, ALLNS=0 */
    __DSB();
    __ISB();
}

void nvic_interrupt_target_state_cfg(void)
{
    /* All external IRQs -> Non-Secure (the NS app owns WiFi/UART/etc.), matching
     * BOOT_RAM_TZCfg (ITNS[0]=0xFFFFFFFF, ITNS[1]=0x0003FFFF for IRQ0~49).
     * This is the bit the driver-only image left at reset-default (all Secure),
     * which is what cascaded into the QUIC boot SecureFault. */
    for (uint32_t i = 0; i < sizeof(NVIC->ITNS) / sizeof(NVIC->ITNS[0]); i++) {
        NVIC->ITNS[i] = 0xFFFFFFFFu;
    }
}

void nvic_interrupt_enable(void)
{
    /* TF-M owns no secure peripheral IRQs in this bring-up; PPC/secure-IRQ
     * wiring is added with isolation L2/L3. */
}
