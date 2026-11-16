extern void LOGUART_PutChar(unsigned char c);
/* Green2 secure NOR driver, cross-core semaphore + SDK flash timing IPC handshake.
 * ROM functions are resolved by bootloader_km4tz/ameba_rom_symbol_bcut_s.ld.
 * All controller operations execute in secure SRAM and mask IRQs until XIP is
 * restored. No unbounded pointer or physical address is accepted from NS. */
#include "Driver_Flash.h"
#include "tfm_hal_device_header.h"
#include "flash_layout.h"
#include <string.h>
extern uint32_t SYSTIMER_TickGet(void);
extern uint32_t IPC_SEMTake(uint32_t, uint32_t);
extern void IPC_SEMFree(uint32_t);
extern void FLASH_SetSpiMode(void *, void *, uint8_t);
extern unsigned char flash_init_para[];
#define REG32(a) (*(volatile uint32_t *)(a))
#define RAMFUNC __attribute__((section(".ramfunc"), noinline))
#define INLINE static inline __attribute__((always_inline))
INLINE void cache_clean(uint32_t a)
{ __DSB(); REG32(0xe000ef68) = a & ~31u; __DSB(); }
INLINE void cache_invalidate(uint32_t a)
{ __DSB(); REG32(0xe000ef5c) = a & ~31u; __DSB(); }
INLINE uint32_t ticks(void) { return SYSTIMER_TickGet(); }
INLINE int expired(uint32_t start) { return (uint32_t)(ticks() - start) > 32768; }
/* Secure aperture, independent of the mutable NS ROM flash parameter block.
 * Register protocol follows Realtek's ameba_data_flash.c (Apache-2.0).
 * Only 3-byte page program and 4 KiB sector erase are supported on this 4 MiB
 * layout. The separate status service permits only the three BP/CMP modes. */
#define SPIC_REG(o) REG32(0x5011d000 + (o))
#define SPIC_FIFO (*(volatile uint8_t *)0x5011d060)
/* sizeof(FLASH_InitTypeDef) in the Green2 SDK is 52. Snapshot before NS starts;
 * the ROM timing helper takes explicit pointers and does not select commands. */
static uint32_t flash_timing[13];
void green2_secure_flash_init(void)
{ memcpy(flash_timing, flash_init_para, sizeof(flash_timing)); }
INLINE void trace_hex(uint32_t x)
{
    for (int i=28; i>=0; i-=4) { unsigned d=(x>>i)&15; LOGUART_PutChar(d<10?'0'+d:'a'+d-10); }
    LOGUART_PutChar(' ');
}
INLINE int spic_transfer(uint8_t cmd, uint32_t addr, const uint8_t *data,
                         uint32_t n, int with_addr, int receive)
{
    uint32_t start = ticks();
    SPIC_REG(0) = (SPIC_REG(0) & ~0x007fe300u) | (receive ? 0x300 : 0);
    SPIC_REG(0x118) = (SPIC_REG(0x118) & ~0x000f0000) | (with_addr ? 0x30000 : 0);
    SPIC_REG(4) = receive ? 1 : 0;
    SPIC_REG(0x130) = receive ? 0 : n;
    SPIC_FIFO = cmd;
    if (with_addr) {
        SPIC_FIFO = addr >> 16; SPIC_FIFO = addr >> 8; SPIC_FIFO = addr;
    }
    uint32_t i = 0;
    while (i < n && (SPIC_REG(0x28) & 2)) SPIC_FIFO = data[i++];
    SPIC_REG(8) = 1;
    while (i < n) {
        if (SPIC_REG(0x28) & 2) SPIC_FIFO = data[i++];
        if (expired(start)) return -1;
    }
    int result = 0;
    if (receive) {
        while (!(SPIC_REG(0x28) & 8)) if (expired(start)) return -1;
        result = SPIC_FIFO;
    }
    while (SPIC_REG(8) & 1) if (expired(start)) return -1;
    return result;
}
INLINE int spic_ready(int want_wel)
{
    uint32_t start = ticks();
    for (;;) {
        int status = spic_transfer(5, 0, 0, 0, 0, 1);
        if (status < 0) return -1;
        if (!(status & 1) && (!want_wel || (status & 2))) return 0;
        if (expired(start)) return -1;
    }
}
RAMFUNC static void spic_modify(uint32_t addr, const uint8_t *data, uint32_t n, int erase)
{
    uint32_t ctrl = SPIC_REG(0);
    uint32_t start = ticks();
    unsigned stage = 0;
    SPIC_REG(0) = ctrl | 0x80000000;
    while (!(SPIC_REG(0) & 0x80000000)) if (expired(start)) goto fail;
    FLASH_SetSpiMode((void *)0x5011d000, flash_timing, 0);
    stage = 1; if (spic_ready(0)) goto fail;
    stage = 2; if (spic_transfer(6,0,0,0,0,0)) goto fail;
    stage = 3; if (spic_ready(1)) goto fail;
    stage = 4; if (spic_transfer(erase ? 0x20 : 2, addr, data, erase ? 0 : n, 1, 0)) goto fail;
    stage = 5; if (spic_ready(0)) goto fail;
    FLASH_SetSpiMode((void *)0x5011d000, flash_timing, ((uint8_t *)flash_timing)[1]);
    SPIC_REG(0) = ctrl;
    __DSB(); __ISB();
    return;
fail:
    LOGUART_PutChar('F');
    LOGUART_PutChar('0' + stage);
    trace_hex(spic_transfer(5,0,0,0,0,1));
    LOGUART_PutChar('\n');
    /* Flash state is unknown: never return to XIP or resume NS after timeout. */
    REG32(0xe000ed0c) = 0x05fa0004;
    __DSB(); for (;;) {}
}
INLINE int sync_peer(uint32_t state)
{
    /* IPC channel 4's own reserved word is the shared flag. No extra NS SRAM
     * allocation, and no key/data buffer is shared with KM4NS. */
    volatile uint32_t *msg = (volatile uint32_t *)0x20004540;
    uint32_t start = ticks();
    while (!(REG32(0x50815088) & (1u << 4)) || (REG32(0x50815000) & (1u << 20)))
        if (expired(start)) return -1;
    msg[0] = 0; msg[1] = 0x2000454c; msg[2] = 1; msg[3] = state;
    cache_clean((uint32_t)msg);
    REG32(0x50815000) = 1u << 20;
    do { cache_invalidate((uint32_t)msg); if (expired(start)) return -1; } while (msg[3]);
    return 0;
}
INLINE void invalidate_flash(uint32_t addr, uint32_t len)
{
    __DSB();
    uint32_t end = 0x08000000 + addr + len;
    for (uint32_t a = (0x08000000 + addr) & ~31u; a < end; a += 32) cache_invalidate(a);
    __DSB(); __ISB();
}
RAMFUNC int green2_secure_flash_modify(uint32_t addr, const void *data, uint32_t len, int erase)
{
    if (addr > FLASH_TOTAL_SIZE || len > FLASH_TOTAL_SIZE - addr || !len ||
        (erase && ((addr & 4095) || len != 4096)) || (!erase && !data)) return -1;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (!IPC_SEMTake(1, 1000)) { __set_PRIMASK(irq); return -1; }
    if (sync_peer(1)) { IPC_SEMFree(1); __set_PRIMASK(irq); return -1; }
    uint32_t start = addr, total = len;
    if (erase) spic_modify(addr, NULL, 0, 1);
    else {
        const unsigned char *p = data;
        unsigned char bounce[256] __attribute__((aligned(32)));
        while (len) {
            uint32_t n = 256 - (addr & 255);
            if (n > len) n = len;
            for (uint32_t i = 0; i < n; ++i) bounce[i] = p[i];
            spic_modify(addr, bounce, n, 0);
            __DSB(); addr += n; p += n; len -= n;
        }
    }
    __DSB(); invalidate_flash(start, total);
    REG32(0x51004018) = 4; /* RSIP MOR.CLEAN_CACHE_LINE, secure aperture */
    __DSB();
    int rc = sync_peer(2);
    IPC_SEMFree(1);
    __set_PRIMASK(irq);
    return rc;
}
static int valid(uint32_t addr, uint32_t len)
{ return addr >= FLASH_ITS_AREA_OFFSET && addr <= FLASH_TOTAL_SIZE && len <= FLASH_TOTAL_SIZE - addr; }
RAMFUNC int green2_secure_flash_status(int32_t mode,uint8_t out[4])
{
    if (mode < -1 || mode > 2) return -1;
    uint32_t irq=__get_PRIMASK(); __disable_irq();
    if (!IPC_SEMTake(1,1000)) { __set_PRIMASK(irq); return -1; }
    if (sync_peer(1)) { IPC_SEMFree(1); __set_PRIMASK(irq); return -1; }
    uint32_t ctrl=SPIC_REG(0),start=ticks();
    SPIC_REG(0)=ctrl|0x80000000;
    while (!(SPIC_REG(0)&0x80000000)) if (expired(start)) goto fatal;
    FLASH_SetSpiMode((void *)0x5011d000,flash_timing,0);
    if (spic_ready(0)) goto fatal;
    int a=spic_transfer(5,0,0,0,0,1),b=spic_transfer(0x35,0,0,0,0,1);
    if (a<0 || b<0) goto fatal;
    out[0]=a; out[2]=b;
    if (mode>=0) {
        uint8_t status[2]={(a&0x83)|(mode==1 ? 0x14 : mode==2 ? 0x1c : 0),
                           (b&~0x40)|(mode==1 ? 0x40 : 0)};
        for (unsigned i=0;i<2;++i) {
            if (spic_transfer(6,0,0,0,0,0) || spic_ready(1) ||
                spic_transfer(i ? 0x31 : 1,0,&status[i],1,0,0) || spic_ready(0)) goto fatal;
        }
        a=status[0]; b=status[1];
    }
    int ra=spic_transfer(5,0,0,0,0,1),rb=spic_transfer(0x35,0,0,0,0,1);
    out[1]=ra; out[3]=rb;
    FLASH_SetSpiMode((void *)0x5011d000,flash_timing,((uint8_t *)flash_timing)[1]);
    SPIC_REG(0)=ctrl; __DSB(); __ISB();
    int rc=sync_peer(2);
    IPC_SEMFree(1); __set_PRIMASK(irq);
    return rc || ra!=a || rb!=b ? -1 : 0;
fatal:
    REG32(0xe000ed0c)=0x05fa0004; __DSB(); for (;;) {}
}
static ARM_DRIVER_VERSION version(void) { return (ARM_DRIVER_VERSION){ARM_FLASH_API_VERSION, 0x100}; }
static ARM_FLASH_CAPABILITIES caps(void) { return (ARM_FLASH_CAPABILITIES){0}; }
static int32_t init(ARM_Flash_SignalEvent_t cb) { (void)cb; return ARM_DRIVER_OK; }
static int32_t uninit(void) { return ARM_DRIVER_OK; }
static int32_t power(ARM_POWER_STATE s) { return s == ARM_POWER_FULL ? ARM_DRIVER_OK : ARM_DRIVER_ERROR_UNSUPPORTED; }
static int32_t read_data(uint32_t a, void *p, uint32_t n)
{
    if (!valid(a,n) || (!p && n)) return ARM_DRIVER_ERROR_PARAMETER;
    memcpy(p, (const void *)(0x08000000+a), n); return n;
}
static int32_t program(uint32_t a, const void *p, uint32_t n)
{
    if (!valid(a,n) || (!p && n)) return ARM_DRIVER_ERROR_PARAMETER;
    if (!n) return 0;
    if (green2_secure_flash_modify(a,p,n,0) || memcmp(p,(const void *)(0x08000000+a),n)) return ARM_DRIVER_ERROR;
    return n;
}
static int32_t erase_sector(uint32_t a)
{
    if (!valid(a,4096) || (a & 4095)) return ARM_DRIVER_ERROR_PARAMETER;
    if (green2_secure_flash_modify(a,NULL,4096,1)) return ARM_DRIVER_ERROR;
    const volatile uint32_t *p = (const volatile uint32_t *)(0x08000000+a);
    for (unsigned i=0;i<1024;++i) if (p[i] != 0xffffffff) return ARM_DRIVER_ERROR;
    return ARM_DRIVER_OK;
}
static int32_t erase_chip(void) { return ARM_DRIVER_ERROR_UNSUPPORTED; }
static ARM_FLASH_STATUS status(void) { return (ARM_FLASH_STATUS){0}; }
static ARM_FLASH_INFO *info(void)
{
    static ARM_FLASH_INFO f = {.sector_count=1024,.sector_size=4096,.page_size=256,.program_unit=1,.erased_value=255};
    return &f;
}
ARM_DRIVER_FLASH Driver_FLASH0 = {version,caps,init,uninit,power,read_data,program,erase_sector,erase_chip,status,info};
