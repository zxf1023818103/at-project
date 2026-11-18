#!/usr/bin/env python3
"""Pack AmebaGreen2 ELF images with the SDK format and bounds-checked flash layout."""
import argparse
import os
from pathlib import Path
import subprocess
import struct
import sys
import shutil

ROOT = Path(__file__).resolve().parents[3]
IMAGE_TOOLS = Path(__file__).resolve().parent
PROJECT = ROOT / 'project/amebagreen2'
sys.path.insert(0, str(ROOT / 'build/amebagreen2/python-deps'))

def validate_core(data, core):
    """Check Green2's XIP, SRAM (including entry table), and PSRAM records."""
    offset = 0
    for index in range(3):
        if offset + 32 > len(data) or data[offset:offset+8] != b'81958711':
            raise ValueError(f'{core}: invalid image header {index}')
        size, address = struct.unpack_from('<II', data, offset + 8)
        if size > 0x170000 or size % 32 or offset + 32 + size > len(data):
            raise ValueError(f'{core}: invalid segment size {index}')
        if index == 0 and address != {'km4ns': 0x02000020, 'km4tz': 0x04000020}[core]:
            raise ValueError(f'{core}: wrong XIP load address')
        if index == 1 and not 0x20005020 <= address < 0x20080000:
            raise ValueError(f'{core}: invalid SRAM entry-table address')
        offset += 32 + size
    if offset != len(data):
        raise ValueError(f'{core}: unexpected data after the three load records')

def validate_ota(data):
    if len(data) < 32:
        raise ValueError('truncated OTA header')
    version, count, sign, hlen, checksum, size, offset, image_id = struct.unpack_from('<8I', data)
    if (count, sign, hlen, offset, image_id) != (1, 0x3141544f, 24, 32, 1):
        raise ValueError('expected a single APP_ALL OTA payload')
    if size != len(data) - 32 or size > 0x170000:
        raise ValueError('OTA payload length/slot limit mismatch')
    if sum(data[32:]) & 0xffffffff != checksum:
        raise ValueError('OTA checksum mismatch')

def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def image(args):
    elf = Path(args.elf).resolve()
    out = elf.parent
    symbols = subprocess.check_output([args.nm, str(elf)], text=True)
    mapfile = out / 'symbols.map'
    mapfile.write_text(symbols)
    def section(name, sections, symbol, boot_index=None):
        if not any(line.split()[-1] == symbol for line in symbols.splitlines()):
            raise ValueError(f'Missing load-address symbol: {symbol}')
        raw = out / (name + '.bin')
        cmd = [args.objcopy, '-O', 'binary']
        for s in sections: cmd += ['-j', s]
        run(*cmd, elf, raw)
        data = raw.read_bytes()
        raw.write_bytes(data + b'\xff' * (-len(data) % 32))
        headed = out / (name + '_prepend.bin')
        extra = ['--boot-index', boot_index] if boot_index else []
        axf('prepend_header', '-i', raw, '-o', headed, '-s', symbol, '-m', mapfile, *extra)
        return headed.read_bytes()
    if args.kind == 'bootloader_km4tz':
        data = section('xip_boot', ['.xip_image1.text', '.gnu.sgstubs'], '__km4tz_boot_text_start__')
        data += section('ram_1', ['.ram_image1.entry', '.ram_image1.text', '.ram_image1.data'],
                        '__ram_image1_text_start__', '0x01010101')
        raw = out / 'km4tz_boot.bin'; raw.write_bytes(data)
        axf('fw_pack', '--image1', raw, '-o', out / 'km4tz_boot_all.bin')
    else:
        ex_end = next(int(line.split()[0], 16) for line in symbols.splitlines()
                      if line.split()[-1] == '__exidx_end')
        exception = ['.ARM.extab', '.ARM.exidx']
        data = section('xip', ['.xip_image2.text'] + (exception if ex_end < 0x20000000 else []), '__flash_text_start__')
        data += section('sram', ['.sram_image2.text.data', '.ram_image2_fill'] +
                        (exception if 0x20000000 <= ex_end < 0x60000000 else []),
                        '__sram_image2_start__', '0x02020202' if args.core == 'km4ns' else None)
        data += section('psram', ['.psram_image2.text.data'] +
                        (exception if ex_end >= 0x60000000 else []), '__psram_image2_start__')
        validate_core(data, args.core)
        (out / f'{args.core}_image2_all.bin').write_bytes(data)

def axf(*args, extern=PROJECT):
    env = os.environ.copy()
    deps = ROOT / 'build/amebagreen2/python-deps'
    if deps.is_dir(): env['PYTHONPATH'] = str(deps) + os.pathsep + env.get('PYTHONPATH', '')
    run(sys.executable, Path(__file__).with_name('axf2bin_compat.py'),
        IMAGE_TOOLS / 'axf2bin.py', '--extern-dir', extern, *args, env=env)

def merge(args):
    from green2_security import prepare, verify_signed, decrypt_rsip
    if (args.sboot == 'ON' or args.rsip == 'ON') and not Path(args.keys).is_file():
        raise FileNotFoundError(
            f'Device security keys missing: {args.keys}. Restore the matching private '
            'key file or set SECURITY_KEYS; refusing to generate replacement device keys.')
    out = Path(args.build).resolve()
    for core in ('km4ns', 'km4tz'):
        validate_core((out/f'app_{core}/{core}_image2_all.bin').read_bytes(), core)
    extern = out / 'security'
    config = prepare(PROJECT / 'manifest.json5', extern, args.keys,
                     args.sboot == 'ON', args.rsip == 'ON')
    # The SDK selects RSIP virtual addresses from its conventional project paths.
    stage = out / 'pack/amebagreen2_gcc_project'
    cores = []
    for core in ('km4ns', 'km4tz'):
        dest = stage / f'project_{core}' / f'{core}_image2_all.bin'
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(out / f'app_{core}/{core}_image2_all.bin', dest)
        cores.append(dest)
    boot_raw = out / 'bootloader_km4tz/km4tz_boot.bin'
    axf('fw_pack', '--image1', boot_raw, '-o', out / 'bootloader_km4tz/km4tz_boot_all.bin', extern=extern)
    boot = (out / 'bootloader_km4tz/km4tz_boot_all.bin').read_bytes()
    app_path = out / 'km4ns_km4tz_app.bin'
    extra, secure = [], b''
    if args.tfm:
        blob = Path(args.tfm).read_bytes()
        if len(blob) < 8 or len(blob) > 0x60000:
            raise ValueError('invalid TF-M image size')
        stack, entry = struct.unpack_from('<II', blob)
        if not 0x3000B000 < stack <= 0x30019000 or not 0x0C000400 <= entry < 0x0C060400 or not entry & 1:
            raise ValueError('TF-M vectors do not match the Green2 secure layout')
        body = bytes(992) + blob
        body += b'\xff' * (-len(body) % 32)
        secure = b'81958711' + struct.pack('<II', len(body), 0x0C000020) + bytes(16) + body
        img3 = stage / 'project_km4tz/km4tz_image3.bin'
        img3.write_bytes(secure)
        extra = ['--image3', img3]
    axf('fw_pack', '--image2', *cores, *extra, '-o', app_path, extern=extern)
    axf('ota_prepend_header', '-i', app_path, '-o', out/'ota_all.bin')
    validate_ota((out/'ota_all.bin').read_bytes())
    app = app_path.read_bytes()
    if args.sboot == 'ON':
        verify_signed(boot, app, boot_raw.read_bytes(), b''.join(p.read_bytes() for p in cores) + secure, config)
        print('Ed25519 boot, certificate, application signatures and SHA-256: PASS')
    if args.rsip == 'ON':
        plain = decrypt_rsip(boot[4096:], 0x10400000, config, 'image1')
        if plain != boot_raw.read_bytes(): raise ValueError('boot RSIP round-trip mismatch')
        offset = 8192
        for core, addr in zip(cores, (0x02000000, 0x04000000)):
            expected = core.read_bytes()
            plain = decrypt_rsip(app[offset:offset+len(expected)], addr, config, 'image2')
            if plain != expected: raise ValueError(f'{core.name} RSIP round-trip mismatch')
            offset += len(expected)
        if secure and decrypt_rsip(app[offset:], 0x0C000000, config, 'image2') != secure:
            raise ValueError('TF-M RSIP round-trip mismatch')
        print('RSIP XTS boot/KM4NS/KM4TZ' + ('/TF-M' if secure else '') + ' independent decrypt comparison: PASS')
    if len(boot) > 0xE000: raise ValueError('bootloader overlaps system/calibration sectors')
    if len(app) > 0x170000: raise ValueError('application exceeds OTA1 slot (0x10000..0x17ffff)')
    data = bytearray(b'\xff' * (0x10000 + len(app)))
    data[:len(boot)] = boot; data[0x10000:] = app
    (out/'firmware.bin').write_bytes(data)
    if args.lfs:
        lfs = Path(args.lfs).read_bytes()
        if len(lfs) != 0xF0000: raise ValueError('LittleFS must be 960 KiB (shared lfs_config geometry)')
        data.extend(b'\xff' * (0x400000-len(data)))
        data[0x300000:0x300000+len(lfs)] = lfs
        (out/'Image_All.bin').write_bytes(data)
    print(f'AmebaGreen2: boot={len(boot)}, app={len(app)}, flash={len(data)} bytes')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('ld'); p.add_argument('output'); p.add_argument('rom')
    p = sub.add_parser('image')
    for name in ['elf', 'core', 'kind', 'objcopy', 'nm']: p.add_argument('--'+name, required=True)
    p = sub.add_parser('merge'); p.add_argument('--build', required=True); p.add_argument('--lfs')
    p.add_argument('--sboot', choices=['ON', 'OFF'], default='OFF')
    p.add_argument('--rsip', choices=['ON', 'OFF'], default='OFF')
    p.add_argument('--keys', default=str(PROJECT / 'keys/com15.json'))
    p.add_argument('--tfm', help='TF-M SPE binary, mapped at 0x0C000400')
    args = parser.parse_args()
    if args.action == 'ld':
        with open(args.output, 'a') as f: f.write(Path(args.rom).read_text())
    elif args.action == 'image': image(args)
    else: merge(args)
