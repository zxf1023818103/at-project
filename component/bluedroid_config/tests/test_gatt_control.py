#!/usr/bin/env python3
"""Host fault-injection checks for the BTU-only response queue.

Mock L2CAP consumes each buffer once and can remain congested, return accepted
congestion, change peer identity, or overflow the bounded response queue.
The callback assertion prevents recursive TX from congestion notifications.
"""
import os
from pathlib import Path
import subprocess
import tempfile

STUBS = {'test.c': '#include <assert.h>\n#include <stdlib.h>\n#include <stdio.h>\n#include "gatt_int.h"\n#include "bluedroid_bouffalo.h"\nstatic unsigned allocated,freed,sent,result; static bool full;\nvoid osi_free(void *p) {assert(p);++freed;free(p);}\nuint64_t bflb_mtimer_get_time_us(void) {return 100;}\nbool L2CA_CheckIsCongest(unsigned cid,const uint8_t *peer) {(void)cid;(void)peer;return full;}\nunsigned attp_send_msg_to_l2cap(tGATT_TCB *t,BT_HDR *p) {(void)t;++sent;osi_free(p);return result;}\nunsigned bluedroid_gatt_control_send(tGATT_TCB *,BT_HDR *);\nstatic BT_HDR *packet(void) {BT_HDR *p=calloc(1,sizeof(*p)+32);assert(p);++allocated;p->len=19;*((uint8_t *)(p+1))=9;return p;}\nint main(void) {\n tGATT_TCB t={.in_use=true,.peer_bda={1,2,3,4,5,6},.att_lcid=4};\n result=GATT_SUCCESS; full=true;\n assert(bluedroid_gatt_control_send(&t,packet())==GATT_SUCCESS);assert(!sent&&!freed);\n for(unsigned i=0;i<10;i++) bluedroid_bouffalo_gatt_control_flush();\n assert(!sent&&!freed);full=false;bluedroid_bouffalo_gatt_control_congestion(false);\n assert(!sent); bluedroid_bouffalo_gatt_control_flush();\n assert(sent==1&&freed==1&&!bluedroid_bouffalo_gatt_control_pending());\n full=true;for(unsigned i=0;i<4;i++) assert(bluedroid_gatt_control_send(&t,packet())==GATT_SUCCESS);\n assert(bluedroid_gatt_control_send(&t,packet())==GATT_NO_RESOURCES);\n bluedroid_bouffalo_gatt_control_reset();assert(allocated==freed);\n full=true;bluedroid_gatt_control_send(&t,packet());t.peer_bda[0]=9;\n bluedroid_bouffalo_gatt_control_flush();assert(!bluedroid_bouffalo_gatt_control_pending()&&allocated==freed);\n full=true;bluedroid_gatt_control_send(&t,packet());result=GATT_CONGESTED;full=false;\n unsigned previous=sent;bluedroid_bouffalo_gatt_control_congestion(false);\n assert(sent==previous); bluedroid_bouffalo_gatt_control_flush();assert(sent==previous+1&&allocated==freed);\n bluedroid_bouffalo_gatt_control_reset();\n puts("PASS: deferred delivery, congestion ownership, hard queue cap, stale-peer cleanup; all allocations freed exactly once");\n}\n', 'gatt_int.h': '#pragma once\n#include <stdbool.h>\n#include <stdint.h>\ntypedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint8_t BD_ADDR[6];\ntypedef unsigned tGATT_STATUS;\ntypedef struct {uint16_t event,len,offset,layer_specific;} BT_HDR;\ntypedef struct {bool in_use; BD_ADDR peer_bda; UINT16 att_lcid;} tGATT_TCB;\n#define GATT_SUCCESS 0\n#define GATT_CONGESTED 143\n#define GATT_NO_RESOURCES 128\n#define L2CAP_ATT_CID 4\nunsigned attp_send_msg_to_l2cap(tGATT_TCB *, BT_HDR *);\n', 'bflb_mtimer.h': '#include <stdint.h>\nuint64_t bflb_mtimer_get_time_us(void);\n', 'bluedroid_bouffalo.h': '#include <stdbool.h>\nvoid bluedroid_bouffalo_gatt_control_reset(void);\nvoid bluedroid_bouffalo_gatt_control_flush(void);\nvoid bluedroid_bouffalo_gatt_control_congestion(bool);\nbool bluedroid_bouffalo_gatt_control_pending(void);\n', 'osi/allocator.h': 'void osi_free(void *);\n', 'common/bt_target.h': '#pragma once\n', 'stack/l2c_api.h': '#include "gatt_int.h"\nbool L2CA_CheckIsCongest(unsigned cid, const uint8_t *peer);\n'}

with tempfile.TemporaryDirectory(prefix="gatt-control-test-") as directory:
    root = Path(directory)
    for relative, content in STUBS.items():
        target = root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)
    source = Path(__file__).resolve().parents[1] / "gatt_control.c"
    binary = root / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", f"-I{root}", str(source),
                    str(root / "test.c"), "-o", str(binary)], check=True)
    env = dict(os.environ)
    # LSan is incompatible with this debugger/container environment; the test
    # separately asserts that allocated/freed buffer counts match exactly.
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    subprocess.run([str(binary)], check=True, env=env)
