/*
 * rlx8721d_freertos_s.ld — minimal GNU-ld scatter for the AmebaD KM4 standalone
 * FreeRTOS secure image. Modeled on the CMSIS-M33 template + TF-M's
 * tfm_common_s.ld, stripped of all the TF-M partition/PSA/BL2 machinery.
 *
 * Addresses are from component/amebad/app_km4_secure/platform/rtl8721d/partition/
 * region_defs.h (kept in sync by hand — hard-coded here to avoid a cpp pass):
 *   S_CODE_START              0x0E400000  (cached-XIP window, bootloader RSIP entry 4)
 *   S_CODE_SIZE               0x00010000  (64 KB)
 *   S_CODE_VECTOR_TABLE_SIZE  0x00000400
 *   CMSE_VENEER_REGION_START  0x0E400400  (= S_CODE_START + vector table size)
 *   S_DATA_START              0x10060000  (secure SRAM)
 *   S_DATA_SIZE               0x00020000  (128 KB)
 *   S_MSP_STACK_SIZE          0x00000800
 *
 * The image starts with the secure vector table (vector[0]=__INITIAL_SP,
 * vector[1]=Reset_Handler) exactly like tfm_s.bin, so the bootloader's secure-jump
 * (boot_flash_hp.c: __set_MSP(xip[0]); call xip[1]) boots it unchanged. objcopy
 * -O binary of the ELF is laid raw at a dynamic flash slot by build_image_all.py
 * (no IMG3 header — the headerless cached-XIP model the TF-M flow already uses).
 */

__STACK_SIZE     = 0x800;   /* S_MSP_STACK_SIZE */
__STACKSEAL_SIZE = 0x8;

MEMORY
{
    FLASH (rx) : ORIGIN = 0x0E400000, LENGTH = 0x00010000
    RAM   (rw) : ORIGIN = 0x10060000, LENGTH = 0x00020000
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* Secure vector table at S_CODE_START; padded to S_CODE_VECTOR_TABLE_SIZE so
     * the veneers land exactly at CMSE_VENEER_REGION_START (0x0E400400). */
    .vectors 0x0E400000 :
    {
        __vectors_start__ = .;
        KEEP(*(.vectors))
        . = ALIGN(4);
        __vectors_end__ = .;
    } > FLASH

    /* CMSE SG veneers occupy the NSC window [0x0E400400, 0x0E400800): they are
     * placed at CMSE_VENEER_REGION_START via -Wl,--section-start,.gnu.sgstubs=
     * 0x0E400400 (this binutils won't assign the section a VMA from an in-script
     * placement under --cmse-implib). .text starts at an explicit 0x0E400800 so it
     * never overlaps that veneer window (a bare `. =` advance is overridden by the
     * `> FLASH` region pointer). */
    .text 0x0E400800 :
    {
        *(.text*)

        KEEP(*(.init))
        KEEP(*(.fini))

        *(.rodata*)

        KEEP(*(.eh_frame*))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    /* .data LMA lives in the cached-XIP window; Reset_Handler copies it to RAM. */
    PROVIDE(__etext = LOADADDR(.data));

    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(vtable)
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Small sbrk heap (the FreeRTOS secure heap is its own static ucHeap in .bss;
     * this only satisfies any stray newlib `end` reference). */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        __end__ = .;
        PROVIDE(end = .);
        . = . + 0x400;
        . = ALIGN(8);
        __HeapLimit = .;
    } > RAM

    .stack (ORIGIN(RAM) + LENGTH(RAM) - __STACK_SIZE - __STACKSEAL_SIZE) (NOLOAD) :
    {
        . = ALIGN(8);
        __STACK_LIMIT = .;
        . = . + __STACK_SIZE;
        . = ALIGN(8);
        __INITIAL_SP = .;
    } > RAM

    .stackseal (ORIGIN(RAM) + LENGTH(RAM) - __STACKSEAL_SIZE) (NOLOAD) :
    {
        . = ALIGN(8);
        __STACK_SEAL = .;
        . = . + 8;
    } > RAM

    PROVIDE(__stack = __INITIAL_SP);

    ASSERT(__STACK_LIMIT >= __HeapLimit, "secure RAM overflow: stack into heap")
}
