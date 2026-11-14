#!/usr/bin/env python3
"""Windows Python + pyserial/bleak/cbor2; real SWS/UART/GATT verification.

Use --flash only after taking a fresh private full-Flash backup with sws_read.ps1.
This test checks the shared CoC PSM discovery; CoC throughput needs the iOS app.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tl3238"))
import ble_verify as common
import serial
from bleak import BleakClient, BleakScanner

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/tl3238_bluedroid"
COC_SERVICE = "b1f01001-97b3-4e38-baf4-91afb3cd92f0"
ECHO_SERVICE = "b1f01002-97b3-4e38-baf4-91afb3cd92f0"
PSM_CHAR = "0000fff4-0000-1000-8000-00805f9b34fb"
ECHO_CHAR = "0000fff5-0000-1000-8000-00805f9b34fb"


async def wireless(args, result, port):
    adv_info = {}
    def match(device, adv):
        if device.address.upper() != args.address.upper() or not (
            adv.local_name == "TL3238-Bluedroid" or COC_SERVICE in adv.service_uuids
        ):
            return False
        adv_info.update(address=device.address, rssi=adv.rssi, name=adv.local_name)
        return True
    device = await BleakScanner.find_device_by_filter(match, timeout=40)
    common.check("Bluedroid advertising", device is not None, adv_info)
    result["advertisement"] = adv_info
    result["cycles"] = []
    for cycle in range(args.cycles):
        if cycle:
            # Wait for the peripheral to advertise again and obtain a fresh
            # Windows BLEDevice; do not reuse a disposed WinRT session.
            device = await BleakScanner.find_device_by_filter(match, timeout=40)
            common.check(f"advertising after disconnect {cycle}", device is not None)
        async with BleakClient(device, timeout=75, winrt={"use_cached_services": False}) as client:
            common.check(f"connect {cycle}", client.is_connected)
            if args.pair:
                await client.pair()
                secure = await client.read_gatt_char("0000fff6-0000-1000-8000-00805f9b34fb")
                common.check("encrypted read", secure == b"hello world")
            common.check("CoC discovery service", client.services.get_service(COC_SERVICE) is not None)
            common.check("echo service", client.services.get_service(ECHO_SERVICE) is not None)
            psm = await client.read_gatt_char(PSM_CHAR)
            common.check("CoC PSM 129", psm == b"\x81\x00", psm.hex())
            replies = asyncio.Queue()
            await client.start_notify(ECHO_CHAR, lambda _, data: replies.put_nowait(bytes(data)))
            maximum = min(244, client.mtu_size - 3)
            count = total = 0
            started = time.monotonic()
            for index in range(args.rounds):
                length = min((1, 20, 100, 244)[index % 4], maximum)
                payload = bytes((cycle * 31 + index * 17 + j) & 255 for j in range(length))
                await client.write_gatt_char(ECHO_CHAR, payload, response=True)
                reply = await asyncio.wait_for(replies.get(), 15)
                common.check(f"notification {cycle}/{index}", reply == payload)
                readback = await client.read_gatt_char(ECHO_CHAR)
                common.check(f"readback {cycle}/{index}", readback == payload)
                count += 1
                total += length
            await client.stop_notify(ECHO_CHAR)
            cccd = await client.read_gatt_descriptor(client.services.get_characteristic(ECHO_CHAR).get_descriptor("00002902-0000-1000-8000-00805f9b34fb").handle)
            common.check("CCCD disabled", cccd == b"\0\0")
            result["cycles"].append(dict(cycle=cycle, packets=count, bytes=total,
                mtu=client.mtu_size, elapsed_seconds=time.monotonic() - started))
        await asyncio.sleep(2)
        common.capture(port, .5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--address", default="60:3F:FB:70:E5:D3")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--cli-only", action="store_true")
    parser.add_argument("--pair", action="store_true", help="pair and read the encrypted probe")
    parser.add_argument("--storage-write", action="store_true", help="create the persistent NVS probe")
    parser.add_argument("--storage-check", action="store_true", help="verify the probe after reset")
    parser.add_argument("--rounds", type=int, default=32)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--output", type=Path, default=OUT / "verification.json")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image = OUT / "firmware/tl3238_bluedroid.bin"
    result = dict(pass_all=False, firmware_bytes=image.stat().st_size,
        firmware_sha256=hashlib.sha256(image.read_bytes()).hexdigest())
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, 115200, 0.1
    port.dtr = port.rts = False
    try:
        port.open()
        if args.flash:
            if not 0 < image.stat().st_size <= 0x70000:
                raise ValueError("Image exceeds partition A")
            subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(ROOT / "project/tl3238_hello/flash.ps1"), "-Firmware", str(image),
                "-SkipSerialVerify"], check=True, timeout=150)
        result["startup"] = common.capture(port, 6)
        for cmd, pattern in [("hello", "hello world"), ("version", "TL3238"),
                             ("ble", "host=Bluedroid status=2"), ("heap", "free="),
                             ("tasks", "BTU_TASK"), ("stats", "fpu_errors=0"),
                             ("selftest", "SELFTEST PASS"), ("idle 1000", "elapsed_ticks=1000"),
                             ("tickless stats", "tickless=1")]:
            result["before_" + cmd] = common.command(port, cmd, pattern)
        if not args.cli_only:
            asyncio.run(wireless(args, result, port))
        if args.storage_write or args.storage_check:
            cmd = "storage test" if args.storage_write else "storage check"
            result["storage"] = common.command(port, cmd, "probe_rc=0")
            common.check("no storage error", "error=0" in result["storage"])
        common.capture(port, 2)
        for cmd, pattern in [("ble", "errors=0"), ("heap", "free="), ("tasks", "BTU_TASK"),
                             ("stats", "sequence_errors=0"), ("selftest", "SELFTEST PASS")]:
            result["after_" + cmd] = common.command(port, cmd, pattern)
        stacks = [int(value) for value in re.findall(r"^.+?\s+[RrBSDI]\s+\d+\s+(\d+)\s*$", result["after_tasks"], re.M)]
        common.check("all task stack margins >= 512 bytes", len(stacks) >= 10 and min(stacks) >= 512, stacks)
        if args.pair:
            common.check("Host has a persistent bond", re.search(r"bonds=[1-9]\d*", result["after_ble"]) is not None)
            common.check("bond store healthy", "storage_error=0" in result["after_ble"])
        common.check("no fatal diagnostics", not any("FATAL" in line or "assertion" in line for line in common.records))
        result["pass_all"] = True
    finally:
        if port.is_open:
            try:
                result["final_uart"] = common.capture(port, 1)
            except Exception as error:
                result["final_uart_error"] = str(error)
                result["pass_all"] = False
        port.close()
        result["checks"] = common.checks
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
        args.output.with_suffix(".log").write_text("\n".join(common.records), encoding="utf-8")
    if not result["pass_all"]:
        raise AssertionError(f"Verification failed; see {args.output}")
    common.log(f"PASS: report {args.output}")


if __name__ == "__main__":
    main()
