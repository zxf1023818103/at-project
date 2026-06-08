/*
 * AmebaD (RTL8721D) KM4 — minimal secure-world image (IMG3) demo.
 *
 * This is the SDK's RDP secure example (project_hp example_sources/rdp/
 * secure_src.c) reduced to a self-contained TrustZone-M secure blob that builds
 * inside OUR decoupled CMake tree — no ameba_soc.h, no ROM lib dependency.
 *
 * What it is: a tiny secure service the Non-Secure (NS) world calls through
 * cmse_nonsecure_entry veneers (.gnu.sgstubs, placed in BD_RAM_NSC @0x1007B000).
 *   - rdp_data[]      : secret data living in BD_RAM_S (0x10077000), which the
 *                       SAU/IDAU marks Secure -> NS reading it faults.
 *   - rdp_protection_entry(idx) : NSC veneer; runs in Secure state, reads the
 *                       secret, returns a derived value. Proves NS->S calls work
 *                       AND that the secret is reachable only from Secure state.
 *   - BOOT_IMG3()     : NSC veneer the NS app calls once early to zero the
 *                       secure .bss (GCC: bounds from the img3 linker script).
 *
 * The KM4 bootloader (boot_flash_hp.c BOOT_FLASH_Image3Load, gated by
 * configENABLE_TRUSTZONE) copies .ram_image3.* -> 0x10077000 and .gnu.sgstubs
 * -> 0x1007B000, and BOOT_RAM_TZCfg() programs the SAU/IDAU so the split is
 * enforced. The secure stack (MSP_S) is set up by the bootloader; this image has
 * no independent reset entry — it is only ever entered through its veneers.
 */

#include <stdint.h>
#include <arm_cmse.h>

typedef uint32_t u32;
typedef uint8_t  u8;

/* Veneer entries must land in .gnu.sgstubs; tagging the function's home section
 * keeps the SDK's section name for parity (the linker still emits the real SG
 * stub into .gnu.sgstubs via the cmse_nonsecure_entry attribute). */
#define IMAGE3_ENTRY_SECTION __attribute__((section(".image3.nsc_entry.text")))
#define NS_ENTRY             __attribute__((cmse_nonsecure_entry))

/*
 * Secret secure data — lives in BD_RAM_S, unreadable from the Non-secure world.
 * (Same values as the SDK RDP demo so the expected results match the app note.)
 */
u32 rdp_data[32] = {
	0x10101010, 0x01010101, 0x02020202, 0x03030303,
	0x04040404, 0x05050505, 0x06060606, 0x07070707,
	0x08080808, 0x09090909, 0x0a0a0a0a, 0x0b0b0b0b,
	0x0c0c0c0c, 0x0d0d0d0d, 0x0e0e0e0e, 0x0f0f0f0f,
	0x10101010, 0x11111111, 0x12121212, 0x13131313,
	0x14141414, 0x15151515, 0x16161616, 0x17171717,
	0x18181818, 0x19191919, 0x1a1a1a1a, 0x1b1b1b1b,
	0x1c1c1c1c, 0x1d1d1d1d, 0x1e1e1e1e, 0x1f1f1f1f
};

/* Internal secure function — NOT a veneer, so NS cannot call it directly. */
static u32 rdp_protection_func(u32 data)
{
	u32 result;

	result = data / 5 + 3;
	result *= 2;
	result += 8;

	return result;
}

/*
 * Secure entry: NS calls this veneer with an index; we read the secret and
 * return f(secret). NS never sees rdp_data itself.
 */
IMAGE3_ENTRY_SECTION
NS_ENTRY u32 rdp_protection_entry(u32 idx)
{
	return rdp_protection_func(rdp_data[idx & 31u]);
}

/* Linker (rlx8721d_img3_s.ld) provides the secure .bss bounds. */
extern u8 __image3_bss_start__[];
extern u8 __image3_bss_end__[];

/*
 * Secure init veneer: the NS app calls this once early to zero the secure .bss.
 * (The SDK also seeds a TRNG here via the ADC; omitted for the minimal demo.)
 */
IMAGE3_ENTRY_SECTION
NS_ENTRY void BOOT_IMG3(void)
{
	u8 *p = __image3_bss_start__;

	while (p < __image3_bss_end__)
		*p++ = 0;
}

/*
 * The img3 linker script declares ENTRY(Reset_Handler) for the ELF entry symbol.
 * IMG3 is never reset-entered (only called via veneers), so this is an unused
 * placeholder that just satisfies the linker.
 */
void Reset_Handler(void)
{
	for (;;)
		;
}
