#!/usr/bin/env python3
"""Run the module-to-module benchmark through its host UART (requires pyserial)."""
import argparse
import datetime
import json
from pathlib import Path
import re
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, required=True)
    parser.add_argument('--host', choices=('bl618dg', 'amebagreen2'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mib', type=int, default=64)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    if not 1 <= args.mib <= 256 or not 1 <= args.rounds <= 20:
        parser.error('MiB must be 1..256 and rounds 1..20')
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    capture = bytearray()
    report = {'host': args.host, 'port': args.port, 'baud': args.baud,
              'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'results': results, 'ok': False}
    uart = serial.Serial()
    uart.port, uart.baudrate = args.port, args.baud
    uart.timeout, uart.write_timeout = 0, 2
    uart.dtr = uart.rts = False

    def receive():
        data = uart.read(uart.in_waiting or 1)
        if data:
            capture.extend(data)
            raw.write(data)
            raw.flush()
        time.sleep(.01)

    try:
        with (args.output / 'uart.log').open('wb') as raw:
            uart.open()
            for _ in range(5):
                start = len(capture)
                uart.write(b'\r\nusbbench\r\n')
                until = time.monotonic() + 2
                while time.monotonic() < until:
                    receive()
                if b'HOST connected=1 busy=0' in capture[start:]:
                    break
            else:
                raise RuntimeError('USB host has no ready benchmark peer; see uart.log')
            cases = [(1, 512, 1), (1, 4096, 1), (args.mib, 16384, args.rounds)]
            for mib, block, rounds in cases:
                command = f'usbbench run {mib} {block} 1 {rounds}'
                print(command, flush=True)
                start = len(capture)
                uart.write(('\r\n' + command + '\r\n').encode())
                until = time.monotonic() + args.timeout
                while time.monotonic() < until:
                    receive()
                    data = bytes(capture[start:])
                    if b'[usbbench] suite FAIL' in data:
                        raise RuntimeError('Firmware reported benchmark failure; see uart.log')
                    if b'[usbbench] suite PASS' in data:
                        break
                else:
                    raise TimeoutError('Benchmark did not finish; see uart.log')
                matches = re.findall(rb'USB_BENCH_RESULT (\{[^\r\n]+\})', data)
                if len(matches) != rounds * 2:
                    raise RuntimeError(f'Expected {rounds * 2} results, captured {len(matches)}')
                for i, match in enumerate(matches):
                    row = json.loads(match)
                    expected = 'device_to_host' if i % 2 else 'host_to_device'
                    if not (row['ok'] == 1 and row['host'] == args.host and row['round'] == i // 2 + 1
                            and row['direction'] == expected and row['block'] == block and row['verify'] == 1
                            and row['bytes'] == row['peer_bytes'] == mib * 1024 * 1024
                            and row['bad_words'] == row['peer_bad_words'] == row['io_errors'] == 0
                            and row['ms'] > 0 and row['kbps'] == row['bytes'] * 8 // row['ms']):
                        raise RuntimeError(f'Invalid benchmark result: {row}')
                    results.append(row)
                    print(json.dumps(row), flush=True)
            report['ok'] = True
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        if uart.is_open:
            uart.close()
        (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
