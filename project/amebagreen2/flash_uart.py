#!/usr/bin/env python3
"""Green2 UART download with separate reset/boot control and readback verification."""
import argparse
import hashlib
import logging
from pathlib import Path
import re
import struct
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / 'component/amebagreen2/image_tool/Flash'
sys.path[:0] = [str(ROOT / 'build/amebagreen2/python-deps'), str(TOOLS)]
import serial
from base.download_handler import Ameba
from base.device_profile import RtkDeviceProfile
from base.image_info import ImageInfo
from base.json_utils import JsonUtils
from base.rt_settings import RtSettings
from base.errno import ErrType
from base.floader_handler import READ


def checked(result, operation):
    if result != ErrType.OK:
        raise RuntimeError(f'{operation}: {result}')


def open_port(name, baud):
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = name, baud, 0.1
    port.dtr = port.rts = False
    port.open()
    return port


def reset(control, download=False):
    control.rts = True
    time.sleep(0.15)
    control.dtr = download
    time.sleep(0.15)
    control.rts = False
    time.sleep(0.15)
    control.dtr = False


def read_flash(device, start, size):
    data = bytearray()
    for offset in range(0, size, 1024):
        count = min(1024, size - offset)
        request = struct.pack('<BBII', READ, 1, start + offset, 1024)
        status, reply = device.floader_handler.send_request(request, len(request), 5)
        checked(status, f'read 0x{start + offset:08x}')
        # SDK send_request includes the trailing checksum in its return value.
        if len(reply) != 1026 or reply[0] != READ:
            raise RuntimeError(f'Unexpected read response: {len(reply)} bytes')
        data.extend(reply[1:1+count])
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM15')
    parser.add_argument('--boot-port', default='COM19')
    parser.add_argument('--baud', type=int, default=1500000)
    parser.add_argument('--floader', type=Path, default=TOOLS/'Devices/Floaders/floader_amebagreen2.bin')
    parser.add_argument('--allow-encrypted', action='store_true',
                        help='Only for a board already provisioned with matching RSIP keys')
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--image', type=Path, help='firmware.bin (boot + OTA1 only)')
    parser.add_argument('--verify-only', action='store_true', help='Read back --image without programming')
    parser.add_argument('--lfs', type=Path, help='Explicitly replace the 960 KiB LittleFS partition')
    parser.add_argument('--boot-log', type=Path, default=ROOT/'build/amebagreen2/boot.log')
    parser.add_argument('--log-secs', type=int, default=20)
    args = parser.parse_args()
    if not args.backup and not args.image:
        parser.error('Select --backup and/or --image')
    firmware = args.image.read_bytes() if args.image else None
    lfs = args.lfs.read_bytes() if args.lfs else None
    if lfs is not None and (firmware is None or len(lfs) != 0xf0000):
        raise ValueError('--lfs requires --image and a 960 KiB filesystem image')
    if firmware is not None:
        if not 0x12000 < len(firmware) <= 0x180000:
            raise ValueError('Expected boot + OTA1 firmware within the first 1.5 MiB')
        if firmware[0x10000:0x10008] != b'81958711':
            raise ValueError('Missing application certificate')
        if not args.allow_encrypted and (
            firmware[0x1000:0x1008] != bytes.fromhex('999996963fcc66fc') or
            firmware[0x12000:0x12008] != b'81958711'):
            raise ValueError('Encrypted or invalid image; use RSIP=OFF on an unprovisioned board')
    logging.basicConfig(level=logging.INFO, format='%(message)s')
    profile = JsonUtils.load_from_file(str(TOOLS/'Devices/Profiles/RTL8711F_NOR.rdev'))
    profile['Floader'] = str(args.floader.resolve())
    profile['HandshakeBaudrate'] = args.baud
    settings = RtSettings(AutoProgramSpicAddrMode4Byte=0,
                          AutoSwitchToDownloadModeWithDtrRts=0,
                          AutoResetDeviceWithDtrRts=0)
    with open_port(args.boot_port, 115200) as control:
        reset(control, download=True)
        device = Ameba(RtkDeviceProfile(**profile), args.port, args.baud, '',
                       settings, logging.getLogger('green2'))
        try:
            checked(device.prepare(), 'prepare flashloader')
            if args.backup:
                if args.backup.exists():
                    raise FileExistsError(f'Refusing to overwrite backup: {args.backup}')
                original = read_flash(device, 0x08000000, 0x400000)
                args.backup.parent.mkdir(parents=True, exist_ok=True)
                args.backup.write_bytes(original)
                print(f'Backup: {args.backup}, SHA256={hashlib.sha256(original).hexdigest()}', flush=True)
            if firmware is not None:
                if not args.verify_only:
                    # Clear BP and CMP while preserving QE and unrelated NOR status bits.
                    for read_cmd, write_cmd, mask in ((5, 1, 0x7c), (0x35, 0x31, 0x40)):
                        status, value = device.read_flash_status_register(read_cmd, 0)
                        checked(status, 'read NOR status')
                        checked(device.write_flash_status_register(write_cmd, 0, value & ~mask), 'unlock NOR')
                # Keep the calibration/system sectors at 0xe000..0xffff intact.
                parts = [(0, firmware[:0xe000]), (0x10000, firmware[0x10000:])]
                if lfs is not None:
                    parts.append((0x300000, lfs))
                for offset, payload in parts:
                    with tempfile.TemporaryDirectory() as tmp:
                        part = Path(tmp)/'firmware-part.bin'
                        part.write_bytes(payload)
                        info = ImageInfo(ImageName=str(part), StartAddress=0x08000000+offset,
                                         EndAddress=0x08000000+offset+len(payload), MemoryType=1)
                        if not args.verify_only:
                            checked(device._download_image(str(part), info), 'program firmware')
                    actual = read_flash(device, 0x08000000+offset, len(payload))
                    if actual != payload:
                        raise RuntimeError(f'Flash readback mismatch at 0x{offset:x}')
                print('Firmware byte-for-byte readback: PASS', flush=True)
        finally:
            device.clean_up()
        if firmware is not None:
            with open_port(args.port, args.baud) as console:
                reset(control)
                deadline = time.monotonic() + args.log_secs
                log = bytearray()
                while time.monotonic() < deadline:
                    log.extend(console.read(max(console.in_waiting, 1)))
            args.boot_log.parent.mkdir(parents=True, exist_ok=True)
            args.boot_log.write_bytes(log)
            print(log.decode('utf-8', errors='replace'), flush=True)
            checked_log = log.replace(b'\r', b'')
            if re.search(rb'FATAL ERROR|ASSERT|HardFault|SecureFault|TRNG failed| FAIL\b', checked_log):
                raise RuntimeError(f'Boot reported a fault or failed self-test; see {args.boot_log}')
            if b'application ready' not in checked_log and b'entering HCI bridge' not in checked_log:
                raise RuntimeError(f'Application startup failed; see {args.boot_log}')


if __name__ == '__main__':
    main()
