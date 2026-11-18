#!/usr/bin/env python3
"""Native Windows: verify LittleFS and bond counts without exposing keys.

--write-probe writes a separate nonsecret probe file. --reset uses SWS to
reset the board and checks that the file and bond counts survive. No format,
bond deletion or firmware programming is performed.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import serial
import ble_verify as ble


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM19")
    parser.add_argument("--write-probe", action="store_true")
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--bdt", type=Path, default=Path(r"C:\temp\tl3238_hello\bdt"))
    parser.add_argument("--output", type=Path, default=ble.OUTPUT / "storage-verification.json")
    args = parser.parse_args()
    report = dict(result="FAIL", reset=args.reset, probe_written=args.write_probe, checks=ble.checks)
    p = serial.Serial()
    p.port, p.baudrate, p.timeout, p.write_timeout = args.port, 115200, .03, 2
    p.dtr = p.rts = False

    def status():
        value = ble.command(p, "storage", r"LFS ready=1.*error=0")
        match = re.search(r"BONDS ready=1 our=(\d+) peer=(\d+) cccd=(\d+) max=4 writes=(\d+) error=0", value)
        ble.check("Persistent store ready", match is not None, value)
        return dict(zip(("our", "peer", "cccd", "writes"), map(int, match.groups())))

    try:
        p.open()
        # SWS readback resets the MCU before returning. Let asynchronous BLE
        # initialization finish before matching the CLI's final prompt, and
        # retain startup diagnostics instead of discarding them.
        ble.capture(p, 3)
        ble.command(p, "")
        report["before"] = status()
        if args.write_probe:
            ble.command(p, "storage test", r"STORAGE op=1 rc=0")
        ble.command(p, "storage check", r"STORAGE op=2 rc=0")
        report["heap_before"] = ble.command(p, "heap")
        if args.reset:
            tool = args.bdt / "config/Cmd_download_tool.exe"
            result = subprocess.run([str(tool), "1", "TL323X", "rst", "-f"],
                                    cwd=args.bdt, capture_output=True, text=True, timeout=20)
            ble.check("SWS reset", result.returncode == 0 and "Reset MCU ok" in result.stdout,
                      result.stdout + result.stderr)
            ble.capture(p, 4)
            report["after"] = status()
            for key in ("our", "peer", "cccd"):
                ble.check(f"{key} count survives reboot", report["before"][key] == report["after"][key])
            ble.check("Startup loads existing IRK without rewriting", report["after"]["writes"] == 0)
            ble.command(p, "storage check", r"STORAGE op=2 rc=0")
        report["heap_after"] = ble.command(p, "heap")
        report["ble"] = ble.command(p, "ble", r"sync=1")
        report["ota"] = ble.command(p, "ota", r"ready=1")
        report["tasks"] = ble.command(p, "tasks")
        report["result"] = "PASS"
    except Exception as exc:
        report["error"] = repr(exc)
        raise
    finally:
        p.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        args.output.with_suffix(".log").write_text("\n".join(ble.records), encoding="utf-8")


if __name__ == "__main__":
    main()
