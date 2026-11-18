#!/usr/bin/env python3
"""Run under Windows Python: pip install bleak cbor2 pyserial.

Uses COM19 and the PC's BLE adapter. --flash invokes the existing SWS flasher
and checks its readback before testing. Every run releases the serial port.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

import serial
import cbor2
from bleak import BleakClient, BleakScanner

NAME = "TL3238-mcumgr"
NAMES = {NAME, "TL3238-CoC"}
SERVICE = "8d53dc1d-1db7-4cd3-868b-8a527460aa84"
CHAR = "da2e7828-fbce-4e01-ae9e-261174997c48"
# Windows may finish uncached GATT discovery after its first physical link
# attempt has timed out. Keep this separate from the shorter SMP response limit.
CONNECT_TIMEOUT = 60
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "build/tl3238"
records = []
checks = []
started = time.monotonic()


def log(text):
    record = f"[{time.monotonic()-started:.3f}s] {text}"
    print(record, flush=True)
    records.append(record)


def check(name, condition, detail=None):
    checks.append({"name": name, "pass": bool(condition), "detail": detail})
    if not condition:
        raise AssertionError(f"{name}: {detail}")
    log(f"PASS {name}")


def capture(port, duration):
    end = time.monotonic() + duration
    result = bytearray()
    while time.monotonic() < end:
        result.extend(port.read(port.in_waiting or 1))
    text = result.decode("utf-8", errors="replace")
    if text:
        log(text)
    if "FATAL" in text or "assert " in text:
        raise AssertionError(text)
    return text


def command(port, text, expected=None, timeout=5):
    port.write((text + "\r").encode())
    end = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < end:
        data.extend(port.read(port.in_waiting or 1))
        if data.endswith(b"tl3238> "):
            break
    answer = data.decode("utf-8", errors="replace")
    log(answer)
    check("CLI " + text, answer.endswith("tl3238> ") and "FATAL" not in answer
          and (expected is None or re.search(expected, answer)), answer if expected else None)
    return answer


class SMP:
    def __init__(self, client):
        self.client = client
        self.seq = 0
        self.buffer = bytearray()
        self.responses = asyncio.Queue()

    def receive(self, _, data):
        self.buffer.extend(data)
        while len(self.buffer) >= 8:
            size = 8 + int.from_bytes(self.buffer[2:4], "big")
            if len(self.buffer) < size:
                break
            self.responses.put_nowait(bytes(self.buffer[:size]))
            del self.buffer[:size]

    async def request(self, op, group, ident, payload, fragment=None):
        self.seq = (self.seq + 1) & 255
        body = cbor2.dumps(payload)
        packet = struct.pack(">BBHHBB", op, 0, len(body), group, self.seq, ident) + body
        max_chunk = self.client.services.get_characteristic(CHAR).max_write_without_response_size
        chunk = min(fragment or max_chunk, max_chunk)
        for pos in range(0, len(packet), chunk):
            await self.client.write_gatt_char(CHAR, packet[pos:pos+chunk], response=False)
        response = await asyncio.wait_for(self.responses.get(), 8)
        header = struct.unpack(">BBHHBB", response[:8])
        check(f"SMP header {group}/{ident}/{self.seq}",
              header == (op+1, 0, len(response)-8, group, self.seq, ident), header)
        return cbor2.loads(response[8:])


async def wireless(port, rounds, soak_seconds):
    log(f"Scanning for {NAME}")
    advertisement = {}
    def matches(dev, adv):
        if adv.local_name not in NAMES:
            return False
        advertisement.update(address=dev.address, rssi_dbm=adv.rssi)
        return True
    dev = await BleakScanner.find_device_by_filter(matches, timeout=30)
    check("BLE advertisement", dev is not None, advertisement)
    log(f"Advertisement: {advertisement}")
    for cycle in range(2):
        async with BleakClient(dev, timeout=CONNECT_TIMEOUT, winrt={"use_cached_services": False}) as client:
            check("BLE connected", client.is_connected)
            check("SMP service discovered", client.services.get_service(SERVICE) is not None)
            smp = SMP(client)
            await client.start_notify(CHAR, smp.receive)
            for i in range(rounds):
                value = f"hello world {cycle}/{i} " + "x" * (i % 80)
                rsp = await smp.request(2, 0, 0, {"d": value}, fragment=7 if i % 3 == 0 else None)
                check("SMP echo", rsp.get("r") == value, len(value))
            params = await smp.request(0, 0, 6, {})
            check("SMP params", params.get("buf_size") == 512 and params.get("buf_count") == 1, params)
            info = await smp.request(0, 0, 7, {})
            check("SMP OS info", "TL3238" in str(info), info)
            boot = await smp.request(0, 0, 8, {})
            check("SMP boot info", "Telink" in str(boot), boot)
            image_state = await smp.request(0, 1, 0, {})
            if "A/B" in str(boot):
                active = [item for item in image_state.get("images", []) if item.get("active")]
                check("OTA active image state", image_state.get("rc", 0) == 0 and
                      len(active) == 1 and len(active[0].get("hash", b"")) == 32,
                      image_state)
            else:
                check("Image management disabled", image_state.get("rc") == 8, image_state)
            # Impossible nh_len must be rejected immediately, then a valid
            # request on the SAME connection must still work.
            await client.write_gatt_char(CHAR, struct.pack(">BBHHBB", 2, 0, 65535, 0, 200, 0), response=False)
            await asyncio.sleep(0.1)
            recovered = await smp.request(2, 0, 0, {"d": "after-oversized-header"}, fragment=3)
            check("SMP oversized-header recovery", recovered.get("r") == "after-oversized-header")
            boundary = await smp.request(2, 0, 0, {"d": "x" * 127}, fragment=13)
            check("SMP maximum echo size", boundary.get("r") == "x" * 127)
            for size in (128, 140):
                invalid = await smp.request(2, 0, 0, {"d": "x" * size})
                check(f"SMP invalid echo size {size}", invalid.get("rc") == 3, invalid)
            if cycle == 0 and soak_seconds:
                end = time.monotonic() + soak_seconds
                iterations = 0
                while time.monotonic() < end:
                    rsp = await smp.request(2, 0, 0, {"d": f"soak-{iterations}"}, fragment=5)
                    check("BLE soak echo", rsp.get("r") == f"soak-{iterations}")
                    command(port, "selftest", "SELFTEST PASS")
                    iterations += 1
                    await asyncio.sleep(5)
                check("BLE soak duration", iterations > 0, {"seconds": soak_seconds, "iterations": iterations})
            command(port, "ble", r"sync=1.*resets=0")
            command(port, "selftest", "SELFTEST PASS")
            # Leave an incomplete header behind to test disconnect cleanup.
            await client.write_gatt_char(CHAR, bytes([2, 0, 0]), response=False)
            await asyncio.sleep(0.1)
            # WinRT can retain a physical link after closing a client session.
            # Request a real link termination and verify the peripheral state.
            command(port, "ble disconnect", r"rc=0")
            await asyncio.sleep(1)
        await asyncio.sleep(1)
    status = command(port, "ble", r"conn=65535")
    check("BLE physical disconnects", int(re.search(r"disconnects=(\d+)", status)[1]) >= 2)
    async with BleakClient(dev, timeout=CONNECT_TIMEOUT, winrt={"use_cached_services": False}) as client:
        smp = SMP(client)
        await client.start_notify(CHAR, smp.receive)
        rsp = await smp.request(2, 0, 5, {})
        check("SMP deferred reset response", rsp.get("rc", 0) == 0, rsp)
        await asyncio.sleep(3)
    boot = capture(port, 1)
    check("SMP reset boot", "NimBLE ready" in boot, boot)
    command(port, "ble", r"sync=1 conn=65535.*resets=0")
    # Advertising and the SMP transport must work after the requested reboot.
    async with BleakClient(dev, timeout=CONNECT_TIMEOUT, winrt={"use_cached_services": False}) as client:
        smp = SMP(client)
        await client.start_notify(CHAR, smp.receive)
        rsp = await smp.request(2, 0, 0, {"d": "after-reset"}, fragment=4)
        check("SMP echo after reset", rsp.get("r") == "after-reset")
        command(port, "ble disconnect", "rc=0")
        await asyncio.sleep(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--bdt-root", default=r"C:\temp\tl3238_hello\bdt")
    parser.add_argument("--cli-only", action="store_true")
    parser.add_argument("--rounds", type=int, default=12)
    parser.add_argument("--soak-seconds", type=int, default=0)
    parser.add_argument("--firmware", type=Path, default=OUTPUT / "firmware/tl3238_freertos.bin")
    parser.add_argument("--output", type=Path, default=OUTPUT / "ble-verification.json")
    args = parser.parse_args()
    firmware = args.firmware.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"firmware_sha256": hashlib.sha256(firmware.read_bytes()).hexdigest(),
              "port": args.port, "flash_readback_verified": False,
              "requested_soak_seconds": args.soak_seconds, "checks": checks}
    try:
        with serial.Serial(args.port, 115200, timeout=0.03, write_timeout=2) as port:
            if args.flash:
                flashed = subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                str(ROOT / "project/tl3238_hello/flash.ps1"), "-BdtDirectory", args.bdt_root,
                                "-Firmware", str(firmware), "-SkipSerialVerify"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                flash_log = OUTPUT / "ble-flash-output.log" if args.output == OUTPUT / "ble-verification.json" else args.output.with_suffix(".flash.log")
                flash_log.write_text(flashed.stdout, encoding="utf-8")
                flashed.check_returncode()
                report["flash_readback_verified"] = True
                log("SWS flash and byte-for-byte readback passed")
            capture(port, 3)
            command(port, "ble", r"sync=1.*resets=0")
            command(port, "tasks", "nimble")
            command(port, "heap")
            command(port, "selftest", "SELFTEST PASS")
            command(port, "idle 1200", r"elapsed_ticks=120[0-2]")
            command(port, "tickless", "tickless=1")
            if not args.cli_only:
                try:
                    asyncio.run(wireless(port, args.rounds, args.soak_seconds))
                except Exception:
                    # WinRT may keep a physical link after an errored client
                    # session. Restore advertising without hiding the original
                    # failure if a diagnostic command itself fails.
                    for diagnostic in ("ble", "stats", "ble disconnect"):
                        try:
                            command(port, diagnostic)
                        except Exception as diagnostic_error:
                            log(f"Diagnostic {diagnostic}: {diagnostic_error!r}")
                    raise
            command(port, "stats", r"fpu_errors=0")
            report["result"] = "PASS"
    except Exception as exc:
        report.update(result="FAIL", error=repr(exc))
        log(f"FAIL {exc!r}")
        raise
    finally:
        report["elapsed_seconds"] = round(time.monotonic() - started, 3)
        args.output.write_text(
            json.dumps(report, indent=2, default=lambda value: value.hex()), encoding="utf-8")
        log_path = OUTPUT / "ble-COM19.log" if args.output == OUTPUT / "ble-verification.json" else args.output.with_suffix(".log")
        log_path.write_text("\n".join(records), encoding="utf-8")


if __name__ == "__main__":
    main()
