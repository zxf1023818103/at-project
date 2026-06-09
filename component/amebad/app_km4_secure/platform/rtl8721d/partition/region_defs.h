/*
 * RTL8721D (Ameba-D) KM4 — TF-M region definitions (BL2 OFF / TF-M = IMG3).
 *
 * Memory map (from component/amebad/app_km4/rlx8721d_layout_is.ld + hal_platform.h):
 *   SRAM  0x10000000..0x10080000 (512 KB). Low 0x10005000 reserved by ROM
 *         (ROMBSS/RSVD/MSP_NS). Attribution is by ADDRESS (no separate S/NS
 *         alias on SRAM): NS in the low range, Secure in the high range.
 *   XIP   0x0E000000 = KM4 image2 flash window (code executes here).
 *   Peripherals: NS alias 0x40000000, Secure alias 0x50000000.
 *
 * Partition (bring-up):
 *   NS_DATA  0x10005000..0x10060000  (364 KB)  -> existing driver-only app + QUIC
 *   S_DATA   0x10060000..0x10080000  (128 KB)  -> TF-M SPE data/bss/heap/stack
 *   S_CODE   0x0E000000.. +256 KB              -> TF-M SPE (XIP), NSC at its top
 *   NS_CODE  0x0E040000.. +1.5 MB              -> NS app (XIP)
 * The IDAU/SAU in target_cfg.c attribute the SRAM boundary (0x10060000) and the
 * XIP S/NS split (0x0E040000). Raising NS_DATA/S_DATA later only needs these
 * three numbers + target_cfg to stay consistent.
 */
#ifndef __REGION_DEFS_H__
#define __REGION_DEFS_H__

#include "flash_layout.h"

/* ---- stacks / heaps ---- */
#define S_HEAP_SIZE             (0x00000800)
#define S_MSP_STACK_SIZE        (0x00000800)
#define S_PSP_STACK_SIZE        (0x00000800)
#define NS_HEAP_SIZE            (0x00001000)
#define NS_STACK_SIZE           (0x00000400)

/* ---- Secure RAM (high) : TF-M SPE data ---- */
#define S_DATA_START            (0x10060000)
#define S_DATA_SIZE             (0x00020000)   /* 128 KB */
#define S_DATA_LIMIT            (S_DATA_START + S_DATA_SIZE - 1)

/* ---- Non-secure RAM (low) : driver-only app + QUIC ---- */
#define NS_DATA_START           (0x10005000)
#define NS_DATA_SIZE            (0x0005B000)   /* -> ends at S_DATA_START 0x10060000 */
#define NS_DATA_LIMIT           (NS_DATA_START + NS_DATA_SIZE - 1)

#define TOTAL_RAM_SIZE          (S_DATA_SIZE + NS_DATA_SIZE)

/* ---- Secure code : TF-M SPE (cached XIP, executes in place from flash) ----
 * tfm_s executes from the CACHED XIP window 0x0E100000 — and we do NOTHING to
 * map it: the driver image2 (app_km4) already programs the KM4 cached-XIP RSIP
 * MMU to linearly map ALL of flash to 0x0E000000 (offset 0x06000000), so
 * 0x0E100000 -> flash 0x08100000 (where tfm_s.bin is laid) for free. tfm_s just
 * rides that mapping; the bootloader secure-jumps straight to 0x0E100000.
 * (Verified by reading 0x0E100000 == tfm_s.bin[0] with NO extra MMU entry.)
 * IMPORTANT: do NOT call RSIP_MMU_Config for tfm_s — a second MMU entry over
 * 0x0E100000 overlaps image2's mapping and faults (that was the old bug). The
 * flash slot is not RSIP-mask encrypted (rtl8721d_bootcfg RSIP_Mask only covers
 * system data), so XIP fetches see plaintext. 0x0E100000 is 0x400-aligned (valid
 * VTOR). Only data lives in RAM (S_DATA); startup copies .TFM_DATA from its flash
 * LMA (which is itself in the cached XIP window). */
#define S_CODE_START            (0x0E100000)
#define S_CODE_SIZE             (0x00010000)   /* 64 KB cached XIP window for tfm_s */
#define S_CODE_LIMIT            (S_CODE_START + S_CODE_SIZE - 1)
#define S_CODE_VECTOR_TABLE_SIZE (0x00000400)
/* Physical flash slot tfm_s.bin is laid at (build_image_all OFF_IMG3) =
 * S_CODE_START - 0x06000000 (the image2 cached-XIP linear offset). */
#define S_CODE_FLASH_PHYS       (0x08100000)

/* ---- Non-secure code : NS app ----
 * Bring-up NSPE is a tiny RAM-resident NS stub (component/amebad/app_km4_secure/ns_stub),
 * copied by the bootloader to NS SRAM. SPM reads its vector table here (MSP=[0],
 * entry=[1]) and BLXNS-es to it. 0x10010000 is inside the SAU Non-Secure SRAM
 * region (0x10000000..S_CODE_START-1) and is 0x400-aligned for VTOR_NS. */
#define NS_CODE_START           (0x10010000)
#define NS_CODE_SIZE            (0x00010000)   /* 64 KB NS code window (RAM) */
#define NS_CODE_LIMIT           (NS_CODE_START + NS_CODE_SIZE - 1)

/* NS partition (image) start/size, as seen by the bootloader / TF-M. */
#define NS_PARTITION_START      (NS_CODE_START)
#define NS_PARTITION_SIZE       (NS_CODE_SIZE)

/* ---- NS handoff block (app_km4 NSPE) ----
 * When the NS world is the full driver app_km4 (a real AmebaD image2, not the
 * flat stub), its reset vector is not a plain table at a fixed address — the
 * bootloader resolves it at runtime (Image2EntryFun) and stashes 3 words here
 * for the SPM to read back via tfm_hal_get_ns_{MSP,entry_point,VTOR}:
 *   [0]=MSP_NS  [1]=NS entry (RamStartFun)  [2]=NS VTOR (app_km4 VectorNS)
 * Placed in RSVD_RAM_NS (0x10002000..0x10003FFF, reserved) — app_km4's own RAM
 * sections all live at >=0x10005000 (BD_RAM_NS), so this never clashes with its
 * .data/.bss/heap. In the SAU NS SRAM region (<S_DATA_START), so the (secure)
 * SPM can read it before BLXNS. */
#define NS_HANDOFF_BLOCK        (0x10002000)

/* ---- Non-secure-callable (veneer) region ----
 * The TF-M common SPE linker places .gnu.sgstubs right AFTER the secure vector
 * table (observed: tfm_psa_*_veneer @ 0x0E000400 = S_CODE_START + vec table),
 * NOT at the top of S_CODE. The NSC region (target_cfg IDAU/SAU) must cover that
 * actual location or NS calls into the veneers SecureFault at runtime. 0x1000
 * leaves room for more veneers once partitions are enabled. */
#define CMSE_VENEER_REGION_SIZE  (0x00001000)
#define CMSE_VENEER_REGION_START (S_CODE_START + S_CODE_VECTOR_TABLE_SIZE)

/* TF-M secure image starts with the vector table. */
#define S_IMAGE_PRIMARY_AREA_OFFSET (0x0)

/* Boot/SPM shared data (measured-boot data) — reserved at the start of S_DATA.
 * Referenced by the common SPE linker even with BL2 OFF. */
#define BOOT_TFM_SHARED_DATA_SIZE   (0x400)
#define BOOT_TFM_SHARED_DATA_BASE   (S_DATA_START)
#define BOOT_TFM_SHARED_DATA_LIMIT  (BOOT_TFM_SHARED_DATA_BASE + \
                                     BOOT_TFM_SHARED_DATA_SIZE - 1)

#endif /* __REGION_DEFS_H__ */
