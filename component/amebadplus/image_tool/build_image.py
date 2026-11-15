#!/usr/bin/env python3
"""Pack AmebaDplus ELF images with the SDK format and bounds-checked flash layout."""
import argparse
import os
from pathlib import Path
import subprocess
import struct
import sys
import shutil

ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / 'platform/ameba-rtos'
PROJECT = ROOT / 'project/amebadplus'
sys.path.insert(0, str(ROOT / 'build/amebadplus/python-deps'))

def validate_core(data, core):
    """Check the four ROM load records before signing/combining them."""
    offset = 0
    for index in range(4):
        if offset + 32 > len(data) or data[offset:offset+8] != b'81958711':
            raise ValueError(f'{core}: invalid image header {index}')
        size, address = struct.unpack_from('<II', data, offset + 8)
        if size % 32 or offset + 32 + size > len(data):
            raise ValueError(f'{core}: invalid segment size {index}')
        if index == 3 and address != {'km0': 0x20004D20, 'km4': 0x20004DA0}[core]:
            raise ValueError(f'{core}: wrong entry-table load address')
        offset += 32 + size
    if core == 'km0' and len(data) % 4096:
        raise ValueError('KM0 image must be padded to 4 KiB before KM4')
    if any(x != 0xff for x in data[offset:]):
        raise ValueError(f'{core}: unexpected data after entry table')

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
    def section(name, sections, symbol):
        raw = out / (name + '.bin')
        cmd = [args.objcopy, '-O', 'binary']
        for s in sections: cmd += ['-j', s]
        run(*cmd, elf, raw)
        data = raw.read_bytes()
        raw.write_bytes(data + b'\xff' * (-len(data) % 32))
        headed = out / (name + '_prepend.bin')
        axf('prepend_header', '-i', raw, '-o', headed, '-s', symbol, '-m', mapfile)
        return headed.read_bytes()
    if args.kind == 'bootloader_km4':
        data = section('xip_boot', ['.xip_image1.text'], '__km4_boot_text_start__')
        data += section('ram_1', ['.ram_image1.entry', '.ram_image1.text', '.ram_image1.data'], '__ram_start_table_start__')
        raw = out / 'km4_boot.bin'; raw.write_bytes(data)
        axf('fw_pack', '--image1', raw, '-o', out / 'km4_boot_all.bin')
    else:
        data = section('xip', ['.xip_image2.text', '.ARM.extab', '.ARM.exidx'], '__flash_text_start__')
        data += section('sram', ['.sram_image2.text.data', '.ram_image2_fill'], '__sram_image2_start__')
        data += section('psram', ['.psram_image2.text.data'], '__psram_image2_start__')
        data += section('entry', ['.ram_image2.entry'], '__img2_entry_start__' if args.core == 'km0' else '__image2_entry_func__')
        if args.core == 'km0': data += b'\xff' * (-len(data) % 4096)
        validate_core(data, args.core)
        (out / f'{args.core}_image2_all.bin').write_bytes(data)

def axf(*args, extern=PROJECT):
    env = os.environ.copy(); env['AMEBA_SDK'] = str(SDK)
    deps = ROOT / 'build/amebadplus/python-deps'
    if deps.is_dir(): env['PYTHONPATH'] = str(deps) + os.pathsep + env.get('PYTHONPATH', '')
    run(sys.executable, Path(__file__).with_name('axf2bin_compat.py'),
        SDK / 'tools/scripts/axf2bin.py', '--extern-dir', extern, *args, env=env)

def merge(args):
    from dplus_security import prepare, verify_signed, decrypt_rsip
    if (args.sboot == 'ON' or args.rsip == 'ON') and not Path(args.keys).is_file():
        raise FileNotFoundError(
            f'Device security keys missing: {args.keys}. Restore the matching private '
            'key file or set SECURITY_KEYS; refusing to generate replacement device keys.')
    out = Path(args.build).resolve()
    for core in ('km0', 'km4'):
        validate_core((out/f'app_{core}/{core}_image2_all.bin').read_bytes(), core)
    extern = out / 'security'
    config = prepare(PROJECT / 'manifest.json5', extern, args.keys,
                     args.sboot == 'ON', args.rsip == 'ON')
    # The SDK selects RSIP virtual addresses from its conventional project paths.
    stage = out / 'pack/amebadplus_gcc_project'
    cores = []
    for core in ('km0', 'km4'):
        dest = stage / f'project_{core}' / f'{core}_image2_all.bin'
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(out / f'app_{core}/{core}_image2_all.bin', dest)
        cores.append(dest)
    boot_raw = out / 'bootloader_km4/km4_boot.bin'
    axf('fw_pack', '--image1', boot_raw, '-o', out / 'bootloader_km4/km4_boot_all.bin', extern=extern)
    boot = (out / 'bootloader_km4/km4_boot_all.bin').read_bytes()
    app_path = out / 'km0_km4_app.bin'
    extra, secure = [], b''
    if args.tfm:
        blob = Path(args.tfm).read_bytes()
        if len(blob) < 8 or len(blob) > 0x60000:
            raise ValueError('invalid TF-M image size')
        stack, entry = struct.unpack_from('<II', blob)
        if not 0x3000B000 < stack <= 0x30019000 or not 0x0F000400 <= entry < 0x0F060400 or not entry & 1:
            raise ValueError('TF-M vectors do not match the Dplus secure layout')
        body = bytes(992) + blob
        body += b'\xff' * (-len(body) % 32)
        secure = b'81958711' + struct.pack('<II', len(body), 0x0F000020) + bytes(16) + body
        img3 = stage / 'project_km4/km4_image3.bin'
        img3.write_bytes(secure)
        extra = ['--image3', img3]
    axf('fw_pack', '--image2', *cores, *extra, '-o', app_path, extern=extern)
    if secure and args.rsip == 'ON':
        encrypted = img3.with_name('km4_image3_encrypted.bin')
        # Dplus SDK only offers RDP for image3. Use the application IV and our
        # reserved MMU/OTF entry for the TF-M XIP tail instead.
        axf('rsip', '-i', img3, '-o', encrypted, '--address', '0x0F000000', '-t', 'image2', extern=extern)
        packed = app_path.read_bytes()
        if packed[-len(secure):] != secure: raise ValueError('SDK image3 tail changed')
        app_path.write_bytes(packed[:-len(secure)] + encrypted.read_bytes())
    axf('ota_prepend_header', '-i', app_path, '-o', out/'ota_all.bin')
    validate_ota((out/'ota_all.bin').read_bytes())
    app = app_path.read_bytes()
    if args.sboot == 'ON':
        verify_signed(boot, app, boot_raw.read_bytes(), b''.join(p.read_bytes() for p in cores) + secure, config)
        print('Ed25519 boot, certificate, application signatures and SHA-256: PASS')
    if args.rsip == 'ON':
        plain = decrypt_rsip(boot[4096:], 0x0F800000, config, 'image1')
        if plain != boot_raw.read_bytes(): raise ValueError('boot RSIP round-trip mismatch')
        offset = 8192
        for core, addr in zip(cores, (0x0C000000, 0x0E000000)):
            expected = core.read_bytes()
            plain = decrypt_rsip(app[offset:offset+len(expected)], addr, config, 'image2')
            if plain != expected: raise ValueError(f'{core.name} RSIP round-trip mismatch')
            offset += len(expected)
        if secure and decrypt_rsip(app[offset:], 0x0F000000, config, 'image2') != secure:
            raise ValueError('TF-M RSIP round-trip mismatch')
        print('RSIP XTS boot/KM0/KM4 independent decrypt comparison: PASS')
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
    print(f'AmebaDplus: boot={len(boot)}, app={len(app)}, flash={len(data)} bytes')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('ld'); p.add_argument('output'); p.add_argument('rom')
    p = sub.add_parser('image')
    for name in ['elf', 'core', 'kind', 'objcopy', 'nm']: p.add_argument('--'+name, required=True)
    p = sub.add_parser('merge'); p.add_argument('--build', required=True); p.add_argument('--lfs')
    p.add_argument('--sboot', choices=['ON', 'OFF'], default='ON')
    p.add_argument('--rsip', choices=['ON', 'OFF'], default='OFF')
    p.add_argument('--keys', default=str(ROOT / 'project/amebadplus/keys/com42.json'))
    p.add_argument('--tfm', help='TF-M SPE binary, mapped at 0x0F000400')
    args = parser.parse_args()
    if args.action == 'ld':
        with open(args.output, 'a') as f: f.write(Path(args.rom).read_text())
    elif args.action == 'image': image(args)
    else: merge(args)
