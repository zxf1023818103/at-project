ENTRY(Reset_Handler)

INCLUDE "rlx8721d_layout_is.ld"

SECTIONS
{
	.rom.text : { } > IROM_NS
	.rom.rodata : { } > DROM_NS
	.hal.rom.bss : { } > ROMBSS_RAM_COM
	.hal.ns_rom.bss : { } > ROMBSS_RAM_NS

	/* image2: normal image start */
	.ram_image2.entry :
	{
		__ram_image2_text_start__ = .;
		__image2_entry_func__ = .;
		KEEP(*(SORT(.image2.entry.data*)))

		__image2_validate_code__ = .;
		KEEP(*(.image2.validate.rodata*)) 

	} > BD_RAM_NS

	.ram_image2.text :
	{
		__ram_text_start__ = .;
		*(.image2.ram.text*)
		*(.image2.net.ram.text*)
		/* Console RX IRQ chain in SRAM (AN0400 §18.2): the LOGUART RX ISR (cli_loguart_rx_isr,
		 * pinned via IMAGE2_RAM_TEXT_SECTION) can fire while the SPE is programming flash (SPIC
		 * user mode), when XIP fetch is unavailable — so every function it executes must run from
		 * RAM too. Pull ONLY those functions (not the whole TUs): RAM .text grows the loaded image
		 * and pushes __bss_end__ up, shrinking the NS heap (__bss_end__..__sram_top__) that the
		 * TLS handshake runs to the edge, so keep the footprint minimal. The set: the StreamBuffer
		 * send path (stream_buffer.c), the FromISR critical mask + priority-validate (portasm.c +
		 * port.c), and the direct-to-task notify (tasks.c; its list ops are inlined). memcpy is
		 * aliased to ROM _memcpy (--defsym, km4 CMakeLists). The context-switch handlers
		 * (PendSV/SVC) stay in XIP — they were never RAM-resident and the flash path works. */
		*stream_buffer.c.obj(.text.xStreamBufferSendFromISR .text.xStreamBufferSpacesAvailable)
		*stream_buffer.c.obj(.text.prvWriteMessageToBuffer .text.prvWriteBytesToBuffer .text.prvBytesInBuffer)
		*port.c.obj(.text.vPortValidateInterruptPriority)
		*portasm.c.obj(.text.ulSetInterruptMask .text.vClearInterruptMask)
		*tasks.c.obj(.text.xTaskGenericNotifyFromISR)
		__ram_text_end__ = .;
	} > BD_RAM_NS

	.ram_image2.data : 
	{
		__data_start__ = .;
		*(.data*)
		*(.image2.ram.data*)
		*(.image2.net.ram.data*)
		__data_end__ = .;
		__ram_image2_text_end__ = .;
		. = ALIGN(16);	
	} > BD_RAM_NS
  
	.ram_image2.bss :
	{
		__bss_start__ = .;
		*(.bss*)
		*(.image2.ram.bss*)
		*(.image2.net.ram.bss*)
		*(COMMON)
		__bss_end__ = .;
	} > BD_RAM_NS

	.ram_image2.nocache.data :
	{
		. = ALIGN (32);
		__ram_nocache_start__ = .;
		*(.bdsram.data*)
		*(.image2.ram.bdsram.data*)
		*(.image2.net.ram.bdsram.data*)
		. = ALIGN (32);
		__ram_nocache_end__ = .;
	} > BD_RAM_NS

	.ram_heap.data : 
	{
		. = ALIGN (32);
		*(.bfsram.data*)
		*(.image2.ram.bfsram.data*)
		*(.image2.net.ram.bfsram.data*)
		end = .;
		*(.heap.stdlib*)
		. = . + 4096;
		__bfsram_end__ = .;
	} > BD_RAM_NS

	/* FIX (sp_tohex boot hang): force the section's VMA to KM4_IMG2 ORIGIN
	 * (0x0E000020) regardless of its alignment. A 0x40-aligned input (e.g. from
	 * sp_int.c) would otherwise raise the output section alignment to 0x40 and push
	 * __flash_text_start__ to 0x0E000040 — but the image2 header + the ROM's linear
	 * XIP MMU map the content right after the 32-byte header (XIP 0x0E000020), so a
	 * non-0x20 start displaces all code by the gap -> wild branch -> SecureFault
	 * AUVIOL at boot. Pinning the VMA keeps __flash_text_start__ = 0x0E000020. */
	.xip_image2.text ORIGIN(KM4_IMG2) :
	{
		__flash_text_start__ = .;

		*(.img2_custom_signature*)

		/* LittleFS (littlefs + lfs_config + amebad lfs_port) .text is pinned
		 * LAST in .xip_image2.text so linking it does NOT shift the boot path
		 * (app_start + its early fwlib callees) or the WiFi driver code to new
		 * XIP addresses. The RTL8721D mis-fetches instructions by one cache-line
		 * at certain physical flash addresses (doc/amebad_xip_layout_boot_hang.md):
		 * any address shift is layout roulette — it deterministically hangs boot
		 * AND intermittently stalls/faults the WiFi 4-way handshake. Excluding the
		 * LFS archives here keeps every pre-existing object at its known-good
		 * pre-LFS address; only LFS code lands at the (new) tail. */
		/* libstdc++ / libsupc++ are pinned to the tail alongside LittleFS for the
		 * same reason: the C++ self-test (AMEBAD_CPP_SELFTEST) pulls a large amount
		 * of iostream/filesystem .text, and letting it land mid-image would shift
		 * the boot/WiFi code to new XIP addresses (the layout boot-hang). When the
		 * test is off nothing references them, so these patterns match nothing. */
		*(EXCLUDE_FILE(*liblittlefs.a *liblfs_config.a *libamebad_lfs_port.a *libstdc++.a *libsupc++.a) .text*)
		*liblittlefs.a:(.text*)
		*liblfs_config.a:(.text*)
		*libamebad_lfs_port.a:(.text*)
		*libstdc++.a:(.text*)
		*libsupc++.a:(.text*)
	/*	*(.image2.net.ram.text*) */
		*(.rodata*)
		*(.image2.ram.rodata*)
		*(.image2.net.ram.rodata*)

		/* Add This for C++ support */
		. = ALIGN(4);
		__preinit_array_start = .;
		KEEP(*(.preinit_array))
		__preinit_array_end = .;
		. = ALIGN(4);
		__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;
		. = ALIGN(4);
		__fini_array_start = .;
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array))
		__fini_array_end = .;	
		/*-----------------*/		

		. = ALIGN (4);
		__cmd_table_start__ = .;
		KEEP(*(.cmd.table.data*))
		__cmd_table_end__ = .;

		__flash_text_end__ = .;
	
		. = ALIGN (16);
	} > KM4_IMG2

	/* Add This for C++ support */
	.ARM.extab  : 
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*) 

	} > KM4_IMG2

	.ARM.exidx :
	{
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > KM4_IMG2
	/*-----------------*/

	.bluetooth_trace.text :
	{
		__btrace_start__ = .;
		*(.BTTRACE)
		__btrace_end__ = .;
	} > BTRACE

	/* PSRAM_NS image start */
	.psram_image2.text :
	{
		__psram_image2_text_start__ = .;
		*(.psram.text*)
		__psram_image2_text_end__ = .;
	} > PSRAM_NS

	.psram_image2.data : 
	{
		. = ALIGN (32);
		*(.psram.data*)
		. = ALIGN (32);
		*(.psram.rodata*)
	} > PSRAM_NS
  
	.psram_image2.bss :
	{
		. = ALIGN (32);
		__psram_bss_start__ = .;
		*(.psram.bss*)
		__psram_bss_end__ = .;
	} > PSRAM_NS

	.psram_heap.data : 
	{
		. = ALIGN (32);
		*(.psram.heap*)
		*(.image2.net.ram.psram.heap*)
	} > PSRAM_NS
}

/* __sram_end__ caps the NS FreeRTOS heap (heap5: __bss_end__ .. __sram_end__).
 * Normally the full BD_RAM_NS top (0x1007C000). For the secure-world (IMG3) demo
 * the bootloader's BOOT_RAM_TZCfg marks 0x10077000..0x1007BFFF Secure/NSC, so the
 * NS heap must stop at 0x10077000 or it faults — app_km4/CMakeLists.txt passes
 * --defsym=__tz_sram_top__=0x10077000 in that build to clamp it here. */
__sram_end__ = DEFINED(__tz_sram_top__) ? __tz_sram_top__ : (ORIGIN(BD_RAM_NS) + LENGTH(BD_RAM_NS));
