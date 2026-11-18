"""OTP command checks on a host stub; no device or OTP programmer is accessed."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


class EfuseTest(unittest.TestCase):
    def test_confirmation_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'FreeRTOS.h').write_text('typedef int BaseType_t;\n#define pdFALSE 0\n')
            (tmp / 'task.h').write_text('#define taskENTER_CRITICAL() ((void)0)\n#define taskEXIT_CRITICAL() ((void)0)\n')
            (tmp / 'FreeRTOS_CLI.h').write_text('''#include <stddef.h>
typedef struct {const char *name,*help; int (*handler)(char *,size_t,const char *); int count;} CLI_Command_Definition_t;
const char *FreeRTOS_CLIGetParameter(const char *,unsigned,int *);
void FreeRTOS_CLIRegisterCommand(const CLI_Command_Definition_t *);
''')
            (tmp / 'test.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "efuse_debug.h"
int mocked_execute(const struct green2_efuse_request *,uint8_t [32]);
#define green2_efuse_execute mocked_execute
#include "efuse_cli.c"
#undef green2_efuse_execute
int OTP_Read8(uint32_t a,uint8_t *b) { (void)a;(void)b;assert(0);return 0; }
int OTP_Write8(uint32_t a,uint8_t b) { (void)a;(void)b;assert(0);return 0; }
int OTP_LogicalRead(uint8_t *b,uint32_t a,uint32_t n) { (void)b;(void)a;(void)n;assert(0);return 0; }
int OTP_LogicalWrite(uint32_t a,uint32_t n,uint8_t *b) { (void)a;(void)n;(void)b;assert(0);return 0; }
static unsigned calls;
static struct green2_efuse_request last;
int mocked_execute(const struct green2_efuse_request *r,uint8_t out[32])
{ ++calls; last=*r; memset(out,0xff,32); return 0; }
const char *FreeRTOS_CLIGetParameter(const char *s,unsigned n,int *len)
{
    while(n--) { while(*s && *s!=' ') ++s; while(*s==' ') ++s; }
    if(!*s) return NULL;
    const char *end=strchr(s,' '); *len=end ? end-s : (int)strlen(s); return s;
}
void FreeRTOS_CLIRegisterCommand(const CLI_Command_Definition_t *c) { (void)c; }
int main(void)
{
    char out[256];
    const char *bad[]={"efuse_burn 380 ff", "efuse_lwrite 0 ff NO", "efuse_burn 380 zz CONFIRM",
        "efuse_burn 380 f CONFIRM", "efuse_burn 380 ff CONFIRM extra", "efuse_read -1 2",
        "efuse_read 380 1garbage", "efuse_lwrite xyz ff CONFIRM"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) efuse_command(out,sizeof(out),bad[i]);
    assert(calls==0);
    efuse_command(out,sizeof(out),"efuse_burn 380 0102 CONFIRM");
    assert(calls==1 && last.operation==GREEN2_EFUSE_RAW_WRITE && last.confirmation==GREEN2_EFUSE_CONFIRM);
    assert(last.length==2 && last.data[0]==1 && last.data[1]==2);
    struct green2_efuse_request r={.operation=GREEN2_EFUSE_RAW_WRITE,.address=0x380,.length=1};
    uint8_t data[32];
    assert(green2_efuse_execute(&r,data)<0); /* missing confirmation */
    r.confirmation=GREEN2_EFUSE_CONFIRM; r.address=0x1fc;
    assert(green2_efuse_execute(&r,data)<0); /* logical packet area */
    r.address=0x800; assert(green2_efuse_execute(&r,data)<0);
    r.operation=GREEN2_EFUSE_LOGICAL_READ; r.address=0x3ff; r.length=2;
    assert(green2_efuse_execute(&r,data)<0);
    r.address=0; r.length=33; assert(green2_efuse_execute(&r,data)<0);
    r.length=0; assert(green2_efuse_execute(&r,data)<0);
    puts("PASS: parsing, explicit confirmation, raw/logical bounds; no OTP accessed");
}
''')
            exe = tmp / 'test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I'+str(tmp), '-I'+str(HERE),
                            str(tmp/'test.c'), str(HERE/'efuse_debug.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))


if __name__ == '__main__':
    unittest.main()
