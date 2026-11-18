#!/usr/bin/env python3
"""Build the local Ameba SDK's native WHC USB firmware in an isolated source view."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, default=Path.home()/'ameba-rtos')
    p.add_argument('--out', type=Path, default=ROOT/'build/native-whc')
    p.add_argument('--toolchain', type=Path, default=Path.home()/'rtk-toolchain')
    p.add_argument('--bulk-2048', action='store_true',
                   help='Enable CONFIG_WHC_USB_BULK_2048 in an output-local config')
    args = p.parse_args()
    sdk, out = args.sdk.resolve(), args.out.resolve()
    view = out/'sdk'
    paths = subprocess.check_output(['git', '-C', str(sdk), 'ls-files', '-z']).decode().split('\0')
    for name in filter(None, paths):
        source, dest = sdk/name, view/name
        if not source.exists():
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists() or dest.is_symlink():
            continue
        # CMake generates this header in the source directory.
        if source.name == 'build_info.h':
            shutil.copy2(source, dest)
        else:
            dest.symlink_to(source, target_is_directory=source.is_dir())
    libdir = Path('component/soc/amebagreen2/project/project_km4tz/lib/application')
    for source in (sdk/libdir).glob('*'):
        dest = view/libdir/source.name
        if source.is_file() and not dest.exists():
            dest.symlink_to(source)
    scripts = view/'component/wifi/whc/nan_test/module'
    config = scripts/'green2_nan_usb_bt_h4.config'
    if args.bulk_2048:
        # Never write through the source-view symlink into the user's SDK.
        base_config = config.read_text()
        config = out/'green2_nan_usb_bt_h4_bulk2048.config'
        lines = [line for line in base_config.splitlines()
                 if not line.startswith(('CONFIG_WHC_USB_BULK_2048=',
                                         '# CONFIG_WHC_USB_BULK_2048 is not set'))]
        config.write_text('\n'.join(lines) + '\nCONFIG_WHC_USB_BULK_2048=y\n')
    env = os.environ.copy()
    env.update(PATH=str(sdk/'.venv/bin') + os.pathsep + env['PATH'],
               TOOLCHAIN_DIR=str(args.toolchain.resolve()), FIRMWARE_CONFIG=str(config), JOBS='8')
    work = out/'green2-sdk-build'
    subprocess.run(['bash', str(scripts/'build-firmware.sh'), str(work)], env=env, check=True)
    applied_config = (work/'menuconfig/.config').read_bytes()
    bulk_2048 = b'CONFIG_WHC_USB_BULK_2048=y' in applied_config.splitlines()
    if bulk_2048 != args.bulk_2048:
        raise ValueError('Generated CONFIG_WHC_USB_BULK_2048 differs from request')
    boot, app = (work/'boot.bin').read_bytes(), (work/'app.bin').read_bytes()
    if (len(boot) >= 0xe000 or boot[0x1000:0x1008] != bytes.fromhex('999996963fcc66fc') or
            app[:8] != b'81958711' or app[0x2000:0x2008] != b'81958711'):
        raise ValueError('Unexpected original SDK image layout')
    combined = boot.ljust(0x40000, b'\xff') + app
    if len(combined) > 0x180000:
        raise ValueError('Image exceeds test board firmware reservation')
    (out/'green2-native.bin').write_bytes(combined)
    manifest = {
        'sdk': str(sdk),
        'commit': subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip(),
        'status': subprocess.check_output(['git', '-C', str(sdk), 'status', '--short'], text=True),
        'config': config.name,
        'config_sha256': hashlib.sha256(applied_config).hexdigest(),
        'usb_bulk_2048': bulk_2048,
        'app_offset': '0x40000', 'bytes': len(combined),
        'sha256': hashlib.sha256(combined).hexdigest(),
    }
    (out/'green2-build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Native SDK image:', out/'green2-native.bin')


if __name__ == '__main__':
    main()
