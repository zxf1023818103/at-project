/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM4 mask-ROM CMSIS pieces
 * with ROM addresses: the mpu_* MPU HAL (cmsis/mpu.h) plus the CMSIS-DSP FFT
 * tables (arm_math.h: armBitRevIndexTable1024, twiddleCoef_1024; KM4 only).
 * Partitioned out of the KM4 ROM symbol table so they live with the
 * cmsis_rom component. Cat'd via AMEBAD_CMSIS_ROM_KM4_LD. PROVIDE so
 * unreferenced entries cost nothing and a real definition wins.
 */
PROVIDE(mpu_entry_register = 0x10001340);
PROVIDE(mpu_enable = 0x1010cbd5);
PROVIDE(mpu_disable = 0x1010cbed);
PROVIDE(mpu_init = 0x1010cc01);
PROVIDE(mpu_set_mem_attr = 0x1010cc3d);
PROVIDE(mpu_region_cfg = 0x1010ccb1);
PROVIDE(mpu_entry_free = 0x1010cdb5);
PROVIDE(mpu_entry_alloc = 0x1010cdc1);
PROVIDE(armBitRevIndexTable1024 = 0x101c004c);
PROVIDE(twiddleCoef_1024 = 0x101c0e5c);
