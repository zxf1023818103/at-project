#!/usr/bin/env python3
"""Windows BLE/GATT + UART checks for Flash Studio CoC firmware.

This checks discovery, PSM, the embedded stream selftest and existing SMP.
CoreBluetooth CoC data/throughput must be tested with the phone app; this
script deliberately does not label GATT traffic as CoC throughput.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import re
import time

import serial
from bleak import BleakClient, BleakScanner
import ble_verify as ble

SERVICE = "b1f01000-97b3-4e38-baf4-91afb3cd92f0"
PSM = "0000fff4-0000-1000-8000-00805f9b34fb"


async def check_gatt(port, args, report):
    advertising = {}
    def matches(device, adv):
        if device.address.lower() != args.address.lower():
            return False
        uuids = {s.lower() for s in adv.service_uuids}
        if adv.local_name != "TL3238-CoC" or SERVICE not in uuids or ble.SERVICE not in uuids:
            return False
        advertising.update(address=device.address, name=adv.local_name,
                           uuids=sorted(uuids), rssi_dbm=adv.rssi)
        return True
    device = await BleakScanner.find_device_by_filter(matches, timeout=30)
    ble.check("CoC and SMP advertised together", device is not None, advertising)
    report["advertising"] = advertising
    async with BleakClient(device, timeout=45, winrt={"use_cached_services": False}) as client:
        try:
            service = client.services.get_service(SERVICE)
            ble.check("Flash Studio service present", service is not None)
            characteristic = service.get_characteristic(PSM)
            ble.check("FFF4 read characteristic", characteristic is not None and
                      "read" in characteristic.properties)
            value = bytes(await client.read_gatt_char(characteristic))
            ble.check("PSM 0x81 little endian", value == b"\x81\x00", value)
            report["psm"] = int.from_bytes(value, "little")
            smp = ble.SMP(client)
            await client.start_notify(ble.CHAR, smp.receive)
            echo = await smp.request(2, 0, 0, {"d": "SMP with CoC enabled"}, fragment=7)
            ble.check("SMP remains usable", echo.get("r") == "SMP with CoC enabled")
            state = await smp.request(0, 1, 0, {})
            active = next((x for x in state.get("images", []) if x.get("active")), None)
            ble.check("Running image matches local CoC build", active is not None and
                      active.get("hash", b"").hex() == report["firmware_sha256"], state)
            report["active_image"] = active
            ble.command(port, "coc", r"ready=1.*open=0")
        finally:
            if client.is_connected:
                ble.command(port, "ble disconnect", "rc=0")
                await asyncio.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--address", default="60:3F:FB:70:E5:D3")
    parser.add_argument("--output", type=Path, default=ble.OUTPUT / "coc-verification.json")
    parser.add_argument("--firmware", type=Path, default=ble.OUTPUT / "firmware/tl3238_freertos.bin")
    args = parser.parse_args()
    data = args.firmware.read_bytes()
    report = dict(result="FAIL", firmware_sha256=hashlib.sha256(data).hexdigest(),
                  image_bytes=len(data), checks=ble.checks,
                  coc_air_test="NOT_RUN: use Flash Studio iOS L2CAP tab", port=args.port)
    try:
        with serial.Serial(args.port, 115200, timeout=.03, write_timeout=2) as port:
            port.reset_input_buffer()
            ble.command(port, "")
            report["coc_status"] = ble.command(port, "coc", r"ready=1.*open=0")
            ble.command(port, "coc selftest", "COC SELFTEST PASS synthetic=1")
            before = ble.command(port, "heap")
            report["heap_before"] = before
            asyncio.run(check_gatt(port, args, report))
            after = ble.command(port, "heap")
            report["heap_after"] = after
            ble.check("Heap stable after GATT connection",
                      re.search(r"\bfree=(\d+)", before)[1] == re.search(r"\bfree=(\d+)", after)[1])
            report["tasks"] = ble.command(port, "tasks")
            report["stats"] = ble.command(port, "stats", r"rx_dropped=0 rx_errors=0 tx_errors=0")
            ble.command(port, "selftest", "SELFTEST PASS")
            report["result"] = "PASS"
    except Exception as exc:
        report["error"] = repr(exc)
        ble.log(f"CoC checks failed: {exc!r}")
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - ble.started, 3)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, default=lambda v: v.hex()), encoding="utf-8")
        ble.log(f"CoC local/discovery checks: {report['result']}; phone throughput pending")
        args.output.with_suffix(".log").write_text("\n".join(ble.records), encoding="utf-8")
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
