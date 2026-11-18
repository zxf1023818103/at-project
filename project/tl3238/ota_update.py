#!/usr/bin/env python3
"""Upload a native TL3238 image through mcumgr, activate permanently and verify.

Run with Windows Python and requirements-ble.txt. --address is required to
select the intended board. --negative-tests exercises rejected images first;
--resume-test disconnects deliberately during the valid transfer.
"""
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import time

import serial
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError
import ble_verify as ble


def image_version(data):
    if len(data) < 4096 or len(data) > 0x70000:
        raise ValueError("Image must fit the 448 KiB OTA slot")
    fields = [struct.unpack_from("<I", data, offset)[0] for offset in (0x18, 0x20, 0x28, 0x2c, 0x30)]
    if fields[:3] != [len(data), 0x544c4e4b, 0x32334f54] or fields[4] != 0x00010001:
        raise ValueError("Not a TL3238 native OTA format 1 image")
    if any(data[0x15:0x18]):
        raise ValueError("Protected/encrypted/secure-boot image is not supported by this adapter")
    return fields[3]


async def images(smp):
    result = await smp.request(0, 1, 0, {})
    ble.check("Image list", isinstance(result.get("images"), list), result)
    return result["images"]


async def negative_tests(smp, data, report):
    sha = hashlib.sha256(data).digest()
    damaged = bytearray(data[:384]); damaged[0x28] ^= 1
    for name, request in (
        ("Wrong platform header rejected", {"off": 0, "len": len(data), "sha": sha, "data": bytes(damaged)}),
        ("Oversized image rejected", {"off": 0, "len": 0x70001, "sha": sha, "data": data[:384]}),
        ("Short SHA rejected", {"off": 0, "len": len(data), "sha": sha[:31], "data": data[:384]}),
        ("Offset overflow rejected", {"off": 0x100000000, "data": data[:384]}),
    ):
        response = await smp.request(2, 1, 1, request)
        ble.check(name, response.get("rc", 0) != 0, response)
    # A short synthetic transfer deliberately has the WRONG digest. It must
    # never become bootable and is never passed to the activation command.
    sample = bytearray(data[:4096]); struct.pack_into("<I", sample, 0x18, len(sample))
    wrong = bytearray(hashlib.sha256(sample).digest()); wrong[0] ^= 1
    for offset in range(0, len(sample), 384):
        request = {"off": offset, "data": bytes(sample[offset:offset+384])}
        if not offset:
            request.update(len=len(sample), sha=bytes(wrong))
        response = await smp.request(2, 1, 1, request)
        if offset+384 >= len(sample):
            ble.check("Incorrect full-image SHA rejected", response.get("rc", 0) != 0, response)
        else:
            ble.check("Negative-test chunk accepted", response.get("off") == offset+384, response)
    listed = await images(smp)
    ble.check("Corrupt image not listed as candidate", all(item.get("active") for item in listed), listed)
    report["negative_tests_completed"] = True


async def update(port, args, report, data):
    sha = hashlib.sha256(data).digest()
    device = await BleakScanner.find_device_by_filter(
        lambda dev, adv: dev.address.lower() == args.address.lower(), timeout=30)
    ble.check("Target BLE address found", device is not None, args.address)
    offset = 0
    reconnect_done = False
    ready_for_reset = False
    retries = 0
    while not ready_for_reset:
        try:
            async with BleakClient(device, timeout=45, winrt={"use_cached_services": False}) as client:
                try:
                    ble.check("BLE connected", client.is_connected)
                    smp = ble.SMP(client)
                    await client.start_notify(ble.CHAR, smp.receive)
                    ble.log("SMP notifications subscribed; reading image state")
                    listed = await images(smp)
                    if "images_before" not in report:
                        report["images_before"] = listed
                        ble.check("Target differs from active image",
                                  all(item.get("hash") != sha for item in listed if item.get("active")))
                    if args.negative_tests and not report.get("negative_tests_completed"):
                        await negative_tests(smp, data, report)
                        listed = await images(smp)
                    if any(item.get("hash") == sha and not item.get("active") for item in listed):
                        # The last upload response may have been lost: consult
                        # the verified image list before starting another upload.
                        offset = len(data)
                    else:
                        previous = offset
                        response = await smp.request(2, 1, 1,
                            {"off": 0, "len": len(data), "sha": sha, "data": data[:args.chunk]})
                        offset = response.get("off")
                        ble.check("Upload start/resume offset",
                                  isinstance(offset, int) and 0 < offset <= len(data), response)
                        if previous:
                            ble.check("Resume retained acknowledged bytes", offset >= previous, (previous, offset))
                            report["resume_offset"] = offset
                        while offset < len(data):
                            count = min(args.chunk, len(data)-offset)
                            response = await smp.request(2, 1, 1,
                                {"off": offset, "data": data[offset:offset+count]})
                            ble.check("Upload offset", response.get("off") == offset+count, response)
                            offset += count
                            report["uploaded_offset"] = offset
                            if offset % (args.chunk*64) == 0 or offset == len(data):
                                ble.log(f"OTA upload {offset}/{len(data)} ({offset*100//len(data)}%)")
                            if args.resume_test and not reconnect_done and offset >= 16384:
                                reconnect_done = True
                                report["deliberate_disconnect_offset"] = offset
                                break
                        if offset < len(data):
                            continue
                    candidate = next((item for item in await images(smp) if item.get("hash") == sha), None)
                    ble.check("Uploaded image verified and inactive", candidate is not None and
                              not candidate.get("active") and not candidate.get("pending"), candidate)
                    report["heap_after_upload"] = ble.command(port, "heap")
                    report["tasks_after_upload"] = ble.command(port, "tasks")
                    response = await smp.request(2, 1, 0, {"hash": sha, "confirm": False})
                    ble.check("Unsupported trial boot rejected", response.get("rc") == 8, response)
                    response = await smp.request(2, 1, 0, {"hash": sha[:31], "confirm": True})
                    ble.check("Short activation hash rejected", response.get("rc") == 3, response)
                    report["activation_requested"] = True
                    response = await smp.request(2, 1, 0, {"hash": sha, "confirm": True})
                    candidate = next((item for item in response.get("images", []) if item.get("hash") == sha), None)
                    ble.check("Permanent activation pending", candidate is not None and
                              candidate.get("pending") and candidate.get("permanent"), response)
                    report["activation_completed"] = True
                    report["heap_before_reset"] = ble.command(port, "heap")
                    response = await smp.request(2, 1, 5, {})
                    ble.check("Pending boot image protected from erase", response.get("rc", 0) != 0, response)
                    response = await smp.request(2, 0, 5, {})
                    ble.check("Reset acknowledged", response.get("rc", 0) == 0, response)
                    report["reset_acknowledged"] = True
                    ready_for_reset = True
                    await asyncio.sleep(2)
                finally:
                    if client.is_connected and not ready_for_reset:
                        try:
                            ble.command(port, "ble disconnect", "rc=0")
                            await asyncio.sleep(1)
                        except Exception as exc:
                            report.setdefault("cleanup_errors", []).append(repr(exc))
                            ble.log(f"Disconnect cleanup error: {exc!r}")
        except (TimeoutError, OSError, BleakError) as exc:
            # Do not repeat a mutation whose reply may have been lost during
            # activation. Save the partial result for explicit recovery.
            if report.get("activation_requested") or retries >= 2:
                raise
            retries += 1
            report.setdefault("transport_retries", []).append(repr(exc))
            ble.log(f"Transfer reconnect {retries}: {exc!r}")
            ble.command(port, "ble disconnect", "rc=0")
            await asyncio.sleep(2)

    ble.capture(port, 1)
    ble.command(port, "")
    status = ble.command(port, "ota", r"OTA ready=1")
    report["ota_after_reset"] = status
    build = int(re.search(r"\bbuild=(\d+)", status)[1])
    active = int(re.search(r"\bactive=0x([0-9a-f]+)", status)[1], 16)
    ble.check("New image executed in alternate bank", build == report["target_build"] and
              active == (report["initial_active"] ^ 0x80000), (build, hex(active)))
    ble.command(port, "ble", r"sync=1.*resets=0")
    async with BleakClient(device, timeout=45, winrt={"use_cached_services": False}) as client:
        try:
            smp = ble.SMP(client)
            await client.start_notify(ble.CHAR, smp.receive)
            report["images_after_reset"] = await images(smp)
            active = next((item for item in report["images_after_reset"] if item.get("active")), None)
            ble.check("Running flash SHA matches uploaded BIN", active is not None and
                      active.get("hash") == sha and active.get("confirmed"), active)
            report["boot_info"] = await smp.request(0, 0, 8, {})
            response = await smp.request(2, 0, 0, {"d": "after-ota"}, fragment=7)
            ble.check("SMP usable after OTA reboot", response.get("r") == "after-ota", response)
        finally:
            if client.is_connected:
                ble.command(port, "ble disconnect", "rc=0")
                await asyncio.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--address", required=True)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--chunk", type=int, default=384, help="Image bytes per SMP request, 64..512")
    parser.add_argument("--negative-tests", action="store_true")
    parser.add_argument("--resume-test", action="store_true")
    parser.add_argument("--output", type=Path, default=ble.OUTPUT / "ota-upgrade.json")
    args = parser.parse_args()
    if not 64 <= args.chunk <= 512:
        parser.error("--chunk must be in 64..512")
    data = args.firmware.read_bytes()
    report = dict(result="FAIL", target_build=image_version(data), image_bytes=len(data),
                  target_sha256=hashlib.sha256(data).hexdigest(), address=args.address,
                  port=args.port, checks=ble.checks, activation_completed=False)
    try:
        with serial.Serial(args.port, 115200, timeout=.03, write_timeout=2) as port:
            port.reset_input_buffer(); ble.command(port, "")
            before = ble.command(port, "ota", r"OTA ready=1[\s\S]*pending=0")
            ble.check("RTOS interrupt nesting disabled", "plic_preempt=0" in before, before)
            report["ota_before"] = before
            report["initial_active"] = int(re.search(r"\bactive=0x([0-9a-f]+)", before)[1], 16)
            ble.command(port, "ble", r"sync=1.*conn=65535")
            report["heap_before"] = ble.command(port, "heap")
            asyncio.run(update(port, args, report, data))
            report["heap_after"] = ble.command(port, "heap")
            report["tasks_after"] = ble.command(port, "tasks")
            report["selftest_after"] = ble.command(port, "selftest", "SELFTEST PASS")
            report["stats_after"] = ble.command(port, "stats", r"rx_dropped=0 rx_errors=0 tx_errors=0")
            report["result"] = "PASS"
    except Exception as exc:
        report["error"] = repr(exc); ble.log(f"OTA FAIL: {exc!r}")
    finally:
        report["elapsed_seconds"] = round(time.monotonic()-ble.started, 3)
        report["completed_at"] = datetime.now(timezone.utc).isoformat()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, default=lambda value: value.hex()), encoding="utf-8")
        ble.log(f"OTA upgrade result: {report['result']}")
        args.output.with_suffix(".log").write_text("\n".join(ble.records), encoding="utf-8")
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
