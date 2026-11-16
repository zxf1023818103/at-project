#!/usr/bin/env python3
"""Extract a pinned NDL RX function and gate its existing-peer reset branch.

Only CMP/BNE at +0x3c becomes a relocated BL to an ABI-preserving adapter.
The adapter resumes at +0x40 (original reset checks), +0xc0 (keep peer),
or +0xd8 (epilogue for a duplicate request while awaiting NDP Confirm).
The original archive and all other instructions/relocations remain untouched.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

EXPECTED = 'ccd8b4dc90f1e228faf3d46d71cb9ab65b312171529e5f373be0a10005fc8817'
FUNCTION = 'rtw_nan_ndl_nego_rx'
GATE = 'green2_nan_ndl_rx_gate'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('ar', 'objcopy', 'archive', 'output'):
        p.add_argument(name)
    a = p.parse_args()
    data = subprocess.check_output([a.ar, 'p', a.archive, 'rtw_nan_data_ndl.o'])
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise SystemExit('Unverified Green2 NDL object; refusing private ABI patch')
    b = bytearray(data)
    shoff = struct.unpack_from('<I', b, 32)[0]
    entsize, count, names_index = struct.unpack_from('<HHH', b, 46)
    sh = [list(struct.unpack_from('<10I', b, shoff+i*entsize)) for i in range(count)]
    ns = sh[names_index]; names = data[ns[4]:ns[4]+ns[5]]
    sn = [names[s[0]:names.index(b'\0', s[0])].decode() for s in sh]
    text_index = sn.index('.text.'+FUNCTION)
    rel_index = sn.index('.rel.text.'+FUNCTION)
    sym_index = sh[rel_index][6]
    str_index = sh[sym_index][6]
    def contents(i):
        s=sh[i]; return data[s[4]:s[4]+s[5]]
    def replace(i, payload):
        b.extend(b'\0' * (-len(b) % max(4, sh[i][8])))
        sh[i][4]=len(b); sh[i][5]=len(payload); b.extend(payload)
    off = sh[text_index][4]+0x3c
    assert data[off:off+4] == bytes.fromhex('05 2e 3f d1')
    b[off:off+4] = bytes.fromhex('ff f7 fe ff')  # THM_CALL implicit addend -4
    strings = contents(str_index)
    symbols = bytearray(contents(sym_index))
    sym_id = len(symbols)//16
    symbols.extend(struct.pack('<IIIBBH',len(strings),0,0,0x12,0,0))
    keep = {'', '.symtab', '.strtab', '.shstrtab', '.ARM.attributes',
            '.text.'+FUNCTION, '.rel.text.'+FUNCTION}
    keep.update(n for n in sn if n.startswith('.rodata.'))
    kept = {i for i,n in enumerate(sn) if n in keep}
    for pos in range(0,len(symbols),16):
        name,val,size,info,other,index=struct.unpack_from('<IIIBBH',symbols,pos)
        if 0 < index < count and index not in kept and info >> 4:
            struct.pack_into('<IIIBBH',symbols,pos,name,0,0,info,other,0)
    rel = contents(rel_index)
    assert all(struct.unpack_from('<I',rel,i)[0] != 0x3c for i in range(0,len(rel),8))
    replace(str_index,strings+GATE.encode()+b'\0')
    replace(sym_index,symbols)
    replace(rel_index,rel+struct.pack('<II',0x3c,(sym_id<<8)|10))
    for i,s in enumerate(sh):struct.pack_into('<10I',b,shoff+i*entsize,*s)
    with tempfile.TemporaryDirectory(prefix='green2-ndl-') as tmp:
        src=Path(tmp)/'gate.o';src.write_bytes(b)
        subprocess.run([a.objcopy,*['--remove-section='+n for n in sn if n not in keep],
                        '--redefine-sym='+FUNCTION+'=__wrap_'+FUNCTION,str(src),a.output],check=True)
    print('Green2 NDL: pinned RX extracted with authenticated-peer reset gate')


if __name__ == '__main__':main()
