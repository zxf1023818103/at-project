#!/usr/bin/env python3
"""
Over-the-air verification of the AmebaD NimBLE peripheral (P3) from the Windows
host via bleak (WinRT BLE backend). Run with Windows Python (forward-slash UNC
avoids backslash mangling from a WSL shell):

    powershell.exe -NoProfile -Command "python //wsl.localhost/Ubuntu-24.04/home/zxf/at-project/project/amebad/nimble_ble_verify.py"

Scans for 'amebad-nimble', connects, enumerates services, then reads / writes /
reads-back the sample characteristic. Exit 0 = PASS.
"""
import asyncio
import sys

NAME = "amebad-nimble"
SVC = "59462f12-9543-9999-12c8-58b459a2712d"
CHR = "59462f12-9543-9999-12c8-58b459a2712e"

from bleak import BleakScanner, BleakClient


async def main():
    print(f"[bleak] scanning for '{NAME}' (20s) ...", flush=True)
    dev = await BleakScanner.find_device_by_name(NAME, timeout=20.0)
    if dev is None:
        print("[bleak] NOT FOUND")
        return 2
    print(f"[bleak] found {dev.address}  name={dev.name}", flush=True)

    async with BleakClient(dev) as c:
        print(f"[bleak] connected = {c.is_connected}", flush=True)
        for s in c.services:
            print(f"[bleak] svc {s.uuid}")
            for ch in s.characteristics:
                print(f"[bleak]   chr {ch.uuid}  props={','.join(ch.properties)}")

        v = await c.read_gatt_char(CHR)
        print(f"[bleak] read      = {v.hex()}", flush=True)

        await c.write_gatt_char(CHR, bytes([0xA5]), response=True)
        print("[bleak] wrote 0xA5", flush=True)

        v = await c.read_gatt_char(CHR)
        print(f"[bleak] read-back = {v.hex()}  (expect a5)", flush=True)

        ok = (v == bytes([0xA5]))
        print(f"[bleak] RESULT: {'PASS' if ok else 'FAIL'}")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
