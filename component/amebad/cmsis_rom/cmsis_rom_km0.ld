/* ROM symbol table for the AmebaD A-cut (RTL8721D) KM0 mask-ROM CMSIS pieces
 * with ROM addresses: the mpu_* MPU HAL (cmsis/mpu.h) plus the CMSIS-DSP FFT
 * tables (arm_math.h: armBitRevIndexTable1024, twiddleCoef_1024; KM4 only).
 * Partitioned out of the KM0 ROM symbol table so they live with the
 * cmsis_rom component. Cat'd via AMEBAD_CMSIS_ROM_KM0_LD. PROVIDE so
 * unreferenced entries cost nothing and a real definition wins.
 */
PROVIDE(mpu_enable = 0x3359);
PROVIDE(mpu_disable = 0x3371);
PROVIDE(mpu_init = 0x3385);
PROVIDE(mpu_set_mem_attr = 0x33ad);
PROVIDE(mpu_region_cfg = 0x3401);
PROVIDE(mpu_entry_free = 0x34ed);
PROVIDE(mpu_entry_alloc = 0x34f9);
PROVIDE(mpu_entry_register = 0x80664);
