/* Same BP/CMP firmware lock as AmebaD, preserving QE and unrelated SR bits. */
#include "ameba_soc.h"
#include "board_flash_lock.h"
#ifdef AT_ENABLE_TFM
#include "tfm_platform_api.h"
#endif
static int status(int32_t mode,uint8_t result[4])
{
#ifdef AT_ENABLE_TFM
    psa_invec in={&mode,sizeof(mode)}; psa_outvec out={result,4};
    return tfm_platform_ioctl(0x8721d4,&in,&out) == 0;
#else
    FLASH_Write_Lock();
    FLASH_RxCmd(5,1,&result[0]); FLASH_RxCmd(0x35,1,&result[2]);
    uint8_t a=result[0],b=result[2];
    if (mode>=0) {
        uint8_t bits;
        board_flash_sr_for_mode(mode,b,&bits,&b);
        a=(a&0x83)|bits;
        /* Deep-sleep wake boots again; do not rewrite an unchanged NOR SR. */
        if (a != result[0]) FLASH_SetStatus(1,1,&a);
        if (b != result[2]) FLASH_SetStatus(0x31,1,&b);
    }
    FLASH_RxCmd(5,1,&result[1]); FLASH_RxCmd(0x35,1,&result[3]);
    FLASH_Write_Unlock();
    return result[1]==a && result[3]==b;
#endif
}
int board_flash_protect(board_flash_prot_t mode,uint8_t *a,uint8_t *b,uint8_t *c,uint8_t *d)
{
    uint8_t result[4];
    if (mode>BOARD_FLASH_LOCK_ALL || !status(mode,result)) return 0;
    if (a) *a=result[0];
    if (b) *b=result[1];
    if (c) *c=result[2];
    if (d) *d=result[3];
    return 1;
}
int board_flash_read_status(uint8_t *a,uint8_t *b)
{
    uint8_t result[4];
    if (!status(-1,result)) return 0;
    if (a) *a=result[0];
    if (b) *b=result[2];
    return 1;
}
