#!/usr/bin/env python3
"""CS/RAS local checks by default. --peer enables a real CS/RAS peer test.

Install requirements-ble.txt in a native Windows venv to use COM19.
Example (two antenna-equipped boards, second board advertising):
  python cs_verify.py --port COM19 --peer 603ffb70e5d3 --timeout 90
Default distance tests use explicitly synthetic data; --peer verifies two-sided PBR.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import time
import serial

def decode_steps(data):
    result, offset = [], 0
    while offset < len(data):
        if offset + 3 > len(data):
            raise ValueError("truncated CS step header")
        mode, channel, length = data[offset:offset + 3]
        offset += 3
        if mode > 3 or channel > 78 or offset + length > len(data):
            raise ValueError("invalid CS step")
        result.append(dict(mode=mode, channel=channel, data=data[offset:offset + length].hex()))
        offset += length
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--peer", help="CS peer public address, 12 hex digits (enables RF test)")
    parser.add_argument("--random-address", action="store_true")
    parser.add_argument("--timeout", type=float, default=60)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--output", type=Path, default=root / "build/tl3238/cs-verification.json")
    parser.add_argument("--firmware", type=Path, default=root / "build/tl3238/firmware/tl3238_freertos.bin")
    args = parser.parse_args()
    if args.peer and not re.fullmatch(r"[0-9a-fA-F]{12}", args.peer):
        parser.error("--peer requires exactly 12 hexadecimal digits")
    report = dict(result="FAIL", port=args.port, rf_requested=bool(args.peer), checks=[])
    log, started = [], time.monotonic()
    firmware = args.firmware.resolve()
    if firmware.exists():
        binary = firmware.read_bytes()
        report["firmware_sha256"] = hashlib.sha256(binary).hexdigest()
        readback = firmware.parent.parent / "readback.bin"
        report["flash_readback_matches"] = readback.exists() and readback.read_bytes()[:len(binary)] == binary
    try:
        with serial.Serial(args.port, 115200, timeout=.04, write_timeout=2) as port:
            def command(text, pattern=None, timeout=8):
                port.write((text + "\r").encode())
                data, deadline = bytearray(), time.monotonic() + timeout
                while time.monotonic() < deadline:
                    data.extend(port.read(port.in_waiting or 1))
                    if data.rstrip().endswith(b"tl3238>"):
                        break
                else:
                    raise TimeoutError(f"{text}: {data!r}")
                response = data.decode(errors="replace")
                log.append(f"[{time.monotonic()-started:.3f}] {response}")
                if "FATAL:" in response or (pattern and not re.search(pattern, response, re.S)):
                    raise AssertionError(f"{text}: {response}")
                if pattern:
                    report["checks"].append(text)
                return response

            def until(cmd, pattern):
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    value = command(cmd)
                    if re.search(pattern, value, re.S):
                        report["checks"].append(f"wait {cmd}: {pattern}")
                        return value
                    time.sleep(.5)
                raise TimeoutError(f"waiting for {cmd}: {pattern}")

            port.reset_input_buffer()
            command("")
            command("ble", r"NimBLE=1\.10\.0.*sync=1.*resets=0.*errors=0")
            report["capabilities"] = command("cs caps", r"rc=0 ready=1 configs=1 roles=0x03 antennas=1 paths=1.*tx_snr=0x[0-9a-f]{2} host_cs=1")
            command("cs", r"controller_cs=1.*drops=0.*RTOS=TIMER1 CS=TIMER0")
            command("cs invalid", "Unknown cs command")
            command("cs connect invalid", "Usage:")
            command("cs connect 001122334455 extra", "Usage:")
            command("ras", r"RAS service=1 uuid=0x185b features=0x00000004")
            if not args.peer:
                state = command("cs")
                if "conn=65535" in state:
                    for op in ("pair", "remote", "start", "stop", "remove", "security", "reflector"):
                        command("cs " + op, r"rc=7\b")
                for _ in range(10):
                    command("cs caps", "rc=0 ready=1")
                command("ras discover", r"rc=7\b")
                command("ras get", r"rc=7\b")
                command("ras invalid", "Usage:")
                command("ras get extra", "Usage:")
                command("ras offset nope", "Usage:")
                command("ras offset 10001", "Usage:")
                command("ras offset 123", "ras_offset rc=0")
                command("ras", r"DIST valid=0.*offset_mm=123")
                command("ras offset 0", "ras_offset rc=0")
                heap = command("heap", r"heap_total=\d+ free=\d+")
                report["heap_before"] = {k:int(v) for k,v in re.findall(r"(\w+)=(\d+)", heap)}
                before = int(re.search(r"\bfree=(\d+)", heap).group(1))
                for _ in range(3):
                    command("ras selftest", r"RAS SELFTEST PASS synthetic=1 expected_mm=2500 measured_mm=(249\d|250\d|2510).*channels=72.*AES=PASS ECDH=PASS rc=0 ecc_rc=0")
                heap = command("heap", r"heap_total=\d+ free=\d+")
                report["heap_after"] = {k:int(v) for k,v in re.findall(r"(\w+)=(\d+)", heap)}
                if report["heap_after"]["heap_total"] != report["heap_before"]["heap_total"]:
                    raise AssertionError("heap capacity changed")
                if int(re.search(r"\bfree=(\d+)", heap).group(1)) != before:
                    raise AssertionError("heap changed across RAS selftests")
                command("ras", r"available=0.*DIST valid=0.*offset_mm=0")
                report["distance_selftest"] = "PASS: synthetic 2500 mm input; not a radio measurement"
                report["rf_result"] = "NOT_RUN: no antenna/CS peer requested"
            else:
                command("ble disconnect", "rc=0")
                until("cs", "conn=65535")
                op = "connect-random" if args.random_address else "connect"
                command(f"cs {op} {args.peer}", "rc=0")
                until("cs", r"conn=(?!65535)\d+")
                command("cs pair", r"rc=(0|2)\b")
                until("cs", "encrypted=1")
                until("ras", r"client=1 discovering=0")
                command("cs start", "rc=0")
                final = until("cs", r"procedures=[1-9]\d*.*complete=1 overflow=0")
                if not re.search(r"rc=0.*errors=0.*drops=0", final, re.S):
                    raise AssertionError(final)
                raw = command("cs data")
                parts = re.findall(r"offset=(\d+) hex=([0-9a-f]*)", raw)
                data = bytearray()
                for offset, hex_data in parts:
                    if int(offset) != len(data):
                        raise ValueError("noncontiguous CS data")
                    data.extend(bytes.fromhex(hex_data))
                report["steps"] = decode_steps(data)
                if not report["steps"]:
                    raise AssertionError("no RF measurement steps")
                measured = until("ras", r"client_state=0.*DIST valid=1 status=0")
                if not re.search(r"errors=0 rc=0", measured):
                    raise AssertionError(measured)
                report["distance_mm"] = int(re.search(r"\bmm=(-?\d+)", measured).group(1))
                report["distance_status"] = measured
                report["rf_result"] = "PASS: CS, RAS peer transfer and PBR phase-slope estimate (accuracy requires reference measurement)"
                command("cs remove", "rc=0")
                command("ble disconnect", "rc=0")
            command("selftest", "SELFTEST PASS")
            command("stats", "rx_dropped=0 rx_errors=0 tx_errors=0")
            command("ble", r"sync=1.*resets=0.*errors=0")
            report["status"] = command("cs")
            report["ras_status"] = command("ras")
            tasks = command("tasks", "stack_free_bytes")
            report["stack_free_bytes"] = {k:int(v) for k,v in re.findall(
                r"^(.+?)\s+[RrBSDI]\s+\d+\s+(\d+)\s*$", tasks, re.M)}
            for name in ("cli", "IDLE", "producer", "consumer", "Tmr Svc", "nimble", "ble_ll"):
                if report["stack_free_bytes"].get(name, 0) < 512:
                    raise AssertionError(f"insufficient stack margin: {name}")
            report["result"] = "PASS"
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        report["elapsed_seconds"] = round(time.monotonic()-started, 3)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        args.output.with_suffix(".log").write_text("\n".join(log), encoding="utf-8")
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["result"] == "PASS" else 1

if __name__ == "__main__":
    raise SystemExit(main())
