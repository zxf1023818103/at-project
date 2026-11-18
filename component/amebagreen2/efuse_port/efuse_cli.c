#include "efuse_debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AT_ENABLE_TFM
#include "tfm_platform_api.h"
#endif
static int number(const char *command, unsigned n, int base, uint32_t *value)
{
    BaseType_t len; const char *p = FreeRTOS_CLIGetParameter(command,n,&len);
    char token[12], *end;
    if (!p || len < 1 || len >= (int)sizeof(token) || *p == '-') return -1;
    memcpy(token,p,len); token[len]=0;
    unsigned long v = strtoul(token,&end,base);
    if (*end || end == token || v > 0xffffffffUL) return -1;
    *value=v; return 0;
}
static int hex(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static BaseType_t efuse_command(char *out,size_t size,const char *command)
{
    struct green2_efuse_request r={0}; uint8_t result[32];
    int write = !strncmp(command,"efuse_burn ",11) || !strncmp(command,"efuse_lwrite ",13);
    int logical = !strncmp(command,"efuse_l",7);
    r.operation = logical + (write ? 2 : 0);
    if (number(command,1,16,&r.address)) goto usage;
    if (write) {
        BaseType_t len, confirm_len;
        const char *data=FreeRTOS_CLIGetParameter(command,2,&len);
        const char *confirm=FreeRTOS_CLIGetParameter(command,3,&confirm_len);
        if (!data || len<2 || len>64 || (len&1) || !confirm || confirm_len!=7 ||
            memcmp(confirm,"CONFIRM",7)) goto usage;
        r.length=len/2; r.confirmation=GREEN2_EFUSE_CONFIRM;
        for (unsigned i=0;i<r.length;++i) {
            int a=hex(data[2*i]), b=hex(data[2*i+1]);
            if (a<0 || b<0) goto usage;
            r.data[i]=(a<<4)|b;
        }
    } else if (number(command,2,10,&r.length)) goto usage;
    BaseType_t extra;
    if (FreeRTOS_CLIGetParameter(command,write ? 4 : 3,&extra)) goto usage;
    int rc;
#ifdef AT_ENABLE_TFM
    psa_invec in={&r,sizeof(r)}; psa_outvec ov={result,sizeof(result)};
    rc=tfm_platform_ioctl(GREEN2_EFUSE_IOCTL,&in,&ov);
#else
    taskENTER_CRITICAL();
    rc=green2_efuse_execute(&r,result);
    taskEXIT_CRITICAL();
#endif
    if (rc) { snprintf(out,size,"efuse: rejected/failed (%d)\r\n",rc); return pdFALSE; }
    char value[65];
    for (unsigned i=0;i<r.length;++i) snprintf(value+2*i,3,"%02x",result[i]);
    snprintf(out,size,"efuse %s 0x%03lx: %s%s\r\n",logical ? "logical" : "physical",
             (unsigned long)r.address,value,write ? " (verified)" : "");
    return pdFALSE;
usage:
    snprintf(out,size,"read: efuse_[l]read <hex address> <1..32 bytes>\r\n"
             "write: efuse_burn/efuse_lwrite <hex address> <hex data> CONFIRM (irreversible)\r\n");
    return pdFALSE;
}
void efuse_burn_cli_register(void)
{
    static const CLI_Command_Definition_t commands[]={
        {"efuse_read","efuse_read: DEBUG physical OTP read\r\n",efuse_command,-1},
        {"efuse_lread","efuse_lread: DEBUG logical OTP read\r\n",efuse_command,-1},
        {"efuse_burn","efuse_burn: DEBUG physical OTP write; CONFIRM required\r\n",efuse_command,-1},
        {"efuse_lwrite","efuse_lwrite: DEBUG logical OTP write; CONFIRM required\r\n",efuse_command,-1}
    };
    for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) FreeRTOS_CLIRegisterCommand(&commands[i]);
}
