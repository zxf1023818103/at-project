ENTRY(Reset_Handler)

INCLUDE "rlx8721d_layout.ld"

SECTIONS
{
	.rom.text : { } > IROM
	.rom.rodata : { } > DROM
	.hal.rom.bss : { } > ROMBSS_RAM

	.ram_image2.entry :
	{
		__ram_image2_text_start__ = .;
		__image2_entry_func__ = .;
		KEEP(*(SORT(.image2.entry.data*)))

		__image2_validate_code__ = .;
		KEEP(*(.image2.validate.rodata*)) 

	} > BD_RAM

	.ram_image2.text :
	{
		*(.image2.ram.text*)

		/* KM0 boot-hang fix: pull the SysTick/tick + delayed-list scheduler hot path out of XIP into
		 * SRAM so its instruction fetches can't be corrupted by a KM4-SPE flash op holding the shared
		 * SPIC in user mode (AN0400 §18.1: auto/XIP and user/program-erase modes are mutually exclusive).
		 * Symptom without this: KM0 takes an early ROM HardFault in xTaskIncrementTick (delayed-list
		 * deref hits a 0xa5a5a5a5 fill value), ~50% recovered / ~50% hangs at "RRAM:". With the hot path
		 * in SRAM the clean-FS boot is 100% reliable (22/22, HardFault gone). NOTE: this does NOT cover
		 * the heavier-boot-flash case (a persistent TF-M ITS object, e.g. a `set key` cert/key): that
		 * makes the SPE do more boot-time flash and corrupts other/earlier KM0 XIP code — the complete
		 * fix is KM0 IPC-quiesce during SPE flash (FLASH_Write_Lock, unavailable to the bare SPE here). */
		*list.c.obj(.text*)
		*port.c.obj(.text*)
		*tasks.c.obj(.text.xTaskIncrementTick .text.xTaskResumeAll .text.vTaskSwitchContext)
		*tasks.c.obj(.text.prvAddCurrentTaskToDelayedList .text.prvResetNextTaskUnblockTime .text.vTaskDelay)
	} > BD_RAM

	.ram_image2.data :
	{
		__data_start__ = .;
		*(.data*)
		__data_end__ = .;
		__ram_image2_text_end__ = .;
		. = ALIGN(16);	
	} > BD_RAM
  
	.ram_image2.bss :
	{
		__bss_start__ = .;
		*(.bss*)
		*(COMMON)
		*(.FW.ram.bss*)
		__bss_end__ = .;
	} > BD_RAM

	.ram_image2.nocache.data :
	{
		. = ALIGN (32);
		__ram_nocache_start__ = .;
		*(.bdsram.data*)
		. = ALIGN (32);
		__ram_nocache_end__ = .;
	} > BD_RAM

	.ram_heap.data : 
	{
		*(.bfsram.data*)
		*(.heap.stdlib*)
	
	} > BD_RAM

	.heap (COPY):
	{
		__HeapBase = .;
		__end__ = .;
		end = __end__;
		KEEP(*(.heap*))
		__HeapLimit = .;
	} > BD_RAM

	.xip_image2.text : 
	{
		__flash_text_start__ = .;

		*(.img2_custom_signature*)

		*(.text*)    
		*(.rodata*)

		. = ALIGN (4);
		__cmd_table_start__ = .;
		KEEP(*(.cmd.table.data*))
		__cmd_table_end__ = .;

		__flash_text_end__ = .;
	
		. = ALIGN (16);
	} > KM0_IMG2

	.ram_retention.entry : 
	{
		__retention_entry_func__ = .;
		KEEP(*(SORT(.retention.entry.data*)))
	} > RETENTION_RAM

	.ram_retention.text : 
	{
		__retention_text_start__ = .;
		KEEP(*(.retention.ram.text*))
		KEEP(*(.retention.ram.data*))
	} > RETENTION_RAM
}
