#!/usr/bin/env python3
"""
Coex soak: hold a BLE connection to 'amebad-nimble' and hammer the sample
characteristic (write incrementing byte + read-back) for ~DURATION seconds. Run
this overlapping the device's WiFi iperf burst to prove WiFi/BT coex: WiFi keeps
throughput (see the iperf -s log) while BLE read/writes keep succeeding.

    powershell.exe -NoProfile -Command "python //wsl.localhost/Ubuntu-24.04/home/zxf/at-project/project/amebad/nimble_ble_soak.py"
"""
import asyncio
import sys
import time

NAME = "amebad-nimble"
CHR = "59462f12-9543-9999-12c8-58b459a2712e"
DURATION = 30.0

from bleak import BleakScanner, BleakClient


async def main():
    print(f"[soak] scanning for '{NAME}' (40s)...", flush=True)
    dev = await BleakScanner.find_device_by_name(NAME, timeout=40.0)
    if dev is None:
        print("[soak] NOT FOUND")
        return 2
    print(f"[soak] found {dev.address}; connecting...", flush=True)
    async with BleakClient(dev) as c:
        print(f"[soak] connected={c.is_connected}; soaking {DURATION:.0f}s ...", flush=True)
        ok = 0
        bad = 0
        t0 = time.time()
        i = 0
        while time.time() - t0 < DURATION:
            i = (i + 1) & 0xFF
            try:
                await c.write_gatt_char(CHR, bytes([i]), response=True)
                v = await c.read_gatt_char(CHR)
                if v == bytes([i]):
                    ok += 1
                else:
                    bad += 1
            except Exception as e:
                bad += 1
                print(f"[soak] op error: {e}", flush=True)
            await asyncio.sleep(0.05)
        dt = time.time() - t0
        print(f"[soak] {dt:.1f}s: {ok} ok, {bad} bad  ({ok/dt:.1f} rw/s)", flush=True)
        print(f"[soak] RESULT: {'PASS' if bad == 0 and ok > 0 else 'FAIL'}")
        return 0 if (bad == 0 and ok > 0) else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
