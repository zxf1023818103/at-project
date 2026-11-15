#!/usr/bin/env python3
"""Upload this project's ota_all.bin through the standard mcumgr BLE service.

Requires bleak. A disconnect after the final block is expected: confirm the
new bank/version from the serial boot log before treating the update as passed.
"""
import argparse
import asyncio
import hashlib
from pathlib import Path
import struct
import time
from bleak import BleakClient, BleakScanner

CHAR = 'da2e7828-fbce-4e01-ae9e-261174997c48'


def head(kind, n):
    if n < 24:
        return bytes([kind * 32 + n])
    for tag, width in ((24, 1), (25, 2), (26, 4), (27, 8)):
        if n < 1 << (width * 8):
            return bytes([kind * 32 + tag]) + n.to_bytes(width, 'big')
    raise ValueError('CBOR integer too large')


def encode(value):
    if isinstance(value, int):
        return head(0, value)
    if isinstance(value, str):
        data = value.encode()
        return head(3, len(data)) + data
    if isinstance(value, bytes):
        return head(2, len(value)) + value
    if isinstance(value, dict):
        return head(5, len(value)) + b''.join(encode(k) + encode(v) for k, v in value.items())
    raise ValueError('unsupported CBOR type')


def decode(data):
    pos = 0
    def item():
        nonlocal pos
        first = data[pos]; pos += 1
        kind, n = first >> 5, first & 31
        if 24 <= n <= 27:
            width = 1 << (n - 24)
            n = int.from_bytes(data[pos:pos+width], 'big'); pos += width
        if kind == 0:
            return n
        if kind == 1:
            return -1-n
        if kind in (2, 3):
            value = data[pos:pos+n]; pos += n
            return value.decode() if kind == 3 else value
        if kind == 5:
            value = {}
            if first & 31 == 31:
                while data[pos] != 255:
                    key = item(); value[key] = item()
                pos += 1
            else:
                for _ in range(n):
                    key = item(); value[key] = item()
            return value
        if kind == 7 and n in (20, 21, 22):
            return {20: False, 21: True, 22: None}[n]
        raise ValueError('unsupported SMP CBOR response')
    value = item()
    if pos != len(data):
        raise ValueError('trailing CBOR bytes')
    return value


async def upload(args):
    data = Path(args.image).read_bytes()
    if len(data) < 32 or data[8:12] != b'OTA1':
        raise ValueError('expected ota_all.bin, not Image_All.bin')
    if struct.unpack_from('<I', data, 20)[0] != len(data)-32:
        raise ValueError('invalid OTA length')
    if struct.unpack_from('<I', data, 16)[0] != sum(data[32:]) & 0xffffffff:
        raise ValueError('invalid OTA checksum')
    device = await BleakScanner.find_device_by_filter(
        lambda d, a: a.local_name == args.name or d.address.lower() == args.name.lower(), timeout=20)
    if device is None:
        raise RuntimeError('BLE device not found')
    replies = asyncio.Queue()
    received = bytearray()
    def notify(_, fragment):
        received.extend(fragment)
        while len(received) >= 8:
            length = struct.unpack_from('>H', received, 2)[0] + 8
            if len(received) < length:
                return
            packet = bytes(received[:length]); del received[:length]
            replies.put_nowait(packet)
    start = time.monotonic()
    async with BleakClient(device, timeout=20) as client:
        await client.start_notify(CHAR, notify)
        mtu = client.mtu_size - 3
        seq, off, report = 0, 0, 0
        print(f'Connected {device.address}, ATT payload {mtu}, image {len(data)} bytes', flush=True)
        while off < len(data):
            chunk = min(args.chunk, 512, len(data)-off)
            request = {'image': 0, 'off': off, 'data': data[off:off+chunk]}
            if off == 0:
                request.update(len=len(data), sha=hashlib.sha256(data).digest())
            payload = encode(request)
            packet = struct.pack('>BBHHBB', 2, 0, len(payload), 1, seq, 1) + payload
            for pos in range(0, len(packet), mtu):
                await client.write_gatt_char(CHAR, packet[pos:pos+mtu], response=False)
            try:
                reply = await asyncio.wait_for(replies.get(), timeout=20)
            except asyncio.TimeoutError:
                if off+chunk == len(data) and not client.is_connected:
                    print('Final block sent; device disconnected for reboot. Verify serial bank/version.', flush=True)
                    return
                raise
            if reply[6] != seq or reply[0] != 3:
                raise RuntimeError(f'unexpected SMP response: {reply.hex()}')
            result = decode(reply[8:])
            if result.get('rc', 0) or 'err' in result:
                raise RuntimeError(f'SMP rejected offset {off}: {result}')
            next_off = result.get('off')
            if next_off != off+chunk:
                raise RuntimeError(f'SMP offset mismatch: {off} + {chunk} -> {result}')
            off = next_off; seq = (seq+1) & 255
            if off >= report:
                print(f'{off}/{len(data)} ({100*off/len(data):.1f}%) {time.monotonic()-start:.1f}s', flush=True)
                report = off+32768
        print('Upload acknowledged. Verify the new bank/version on the serial console.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--image', required=True)
    parser.add_argument('--chunk', type=int, default=400)
    arguments = parser.parse_args()
    if not 1 <= arguments.chunk <= 512:
        parser.error('--chunk must be in 1..512 (IMG_MGMT_UL_CHUNK_SIZE)')
    asyncio.run(upload(arguments))
