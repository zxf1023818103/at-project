#!/usr/bin/env python3
"""Probe TL3238 OTA availability over real BLE without modifying flash.

Run in native Windows Python with requirements-ble.txt installed. This checks
the mcumgr image-state endpoint, not a firmware upload/activation cycle.
Exit 2 means image management is unsupported; exit 3 means available but an
upgrade has not been tested. A transport/test error exits 1.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

import serial
from bleak import BleakClient, BleakScanner

import ble_verify as ble


async def probe(port, report):
    advertisement = {}

    def matches(device, adv):
        if adv.local_name not in ble.NAMES:
            return False
        advertisement.update(address=device.address, rssi_dbm=adv.rssi)
        return True

    ble.log(f"Scanning for {ble.NAME}: read-only OTA capability check")
    device = await BleakScanner.find_device_by_filter(matches, timeout=30)
    report["advertisement"] = advertisement
    ble.check("BLE advertisement", device is not None, advertisement)
    ble.log(f"Advertisement: {advertisement}")
    async with BleakClient(device, timeout=45, winrt={"use_cached_services": False}) as client:
        try:
            ble.check("BLE connected", client.is_connected)
            ble.check("SMP service discovered", client.services.get_service(ble.SERVICE) is not None)
            smp = ble.SMP(client)
            await client.start_notify(ble.CHAR, smp.receive)
            echo = await smp.request(2, 0, 0, {"d": "ota-probe"})
            ble.check("SMP echo before OTA probe", echo.get("r") == "ota-probe", echo)
            state = await smp.request(0, 1, 0, {})
            report["image_state_response"] = state
            ble.log(f"Image state response: {state}")
            if state.get("rc") == 8:
                report.update(result="UNSUPPORTED", ota_supported=False,
                              reason="Image management group returns MGMT_ERR_ENOTSUP (8)")
            elif state.get("rc", 0) == 0 and isinstance(state.get("images"), list):
                report.update(result="AVAILABLE_NOT_TESTED", ota_supported=True,
                              reason="Image state is available; upload and activation have not been tested")
            else:
                raise AssertionError(f"Unexpected image state response: {state}")
            report["boot_info"] = await smp.request(0, 0, 8, {})
            ble.log(f"Boot info: {report['boot_info']}")
            echo = await smp.request(2, 0, 0, {"d": "after-ota-probe"}, fragment=7)
            ble.check("SMP remains usable after image request", echo.get("r") == "after-ota-probe", echo)
        finally:
            if client.is_connected:
                try:
                    ble.command(port, "ble disconnect", "rc=0")
                    await asyncio.sleep(1)
                except Exception as exc:
                    report["cleanup_error"] = repr(exc)
                    ble.log(f"Disconnect cleanup: {exc!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--output", type=Path, default=ble.OUTPUT / "ota-verification.json")
    args = parser.parse_args()
    report = dict(result="ERROR", port=args.port, ota_supported=None,
                  probe_completed=False, flash_readback_this_run=False,
                  image_uploaded=False, image_activated=False, reboot_requested=False,
                  checks=ble.checks)
    firmware = ble.OUTPUT / "firmware/tl3238_freertos.bin"
    if firmware.is_file():
        report["local_firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    try:
        with serial.Serial(args.port, 115200, timeout=.03, write_timeout=2) as port:
            port.reset_input_buffer()
            ble.command(port, "")
            report["version"] = ble.command(port, "version", "TL3238")
            report["ble_before"] = ble.command(port, "ble", r"sync=1.*conn=65535")
            report["heap_before"] = ble.command(port, "heap", r"heap_total=\d+ free=\d+")
            try:
                asyncio.run(probe(port, report))
                report["probe_completed"] = True
            finally:
                for name in ("ble", "heap", "stats"):
                    try:
                        report[name + "_after"] = ble.command(port, name)
                    except Exception as exc:
                        report[name + "_after_error"] = repr(exc)
    except Exception as exc:
        report["error"] = repr(exc)
        ble.log(f"Probe error: {exc!r}")
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - ble.started, 3)
        report["completed_at"] = datetime.now(timezone.utc).isoformat()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # A supported image-state response can contain binary image hashes.
        args.output.write_text(json.dumps(report, indent=2,
                                          default=lambda value: value.hex()), encoding="utf-8")
        ble.log(f"OTA result: {report['result']}; probe_completed={report['probe_completed']}")
        args.output.with_suffix(".log").write_text("\n".join(ble.records), encoding="utf-8")
    if not report["probe_completed"]:
        return 1
    return 2 if report["result"] == "UNSUPPORTED" else 3


if __name__ == "__main__":
    raise SystemExit(main())
