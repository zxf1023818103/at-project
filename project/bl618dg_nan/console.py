#!/usr/bin/env python3
"""COM37 BL618DG console: DTR=True selects normal boot on this bench."""
import argparse
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM37')
    parser.add_argument('--baud', type=int, default=500000)
    parser.add_argument('--reset', action='store_true', help='Pulse RTS with DTR held at normal boot')
    parser.add_argument('--command', default='nan status')
    parser.add_argument('--seconds', type=float, default=15)
    parser.add_argument('--log', type=Path, default=Path('build/nan-usb/console.log'))
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    args.log.parent.mkdir(parents=True, exist_ok=True)
    uart = serial.Serial()
    uart.port, uart.baudrate = args.port, args.baud
    uart.timeout, uart.write_timeout = 0, 2
    # This board's download strap is active high; pyserial DTR=True drives low.
    # DTR=False permits live logs, but a subsequent reboot enters the BootROM.
    uart.dtr, uart.rts = True, False
    try:
        uart.open()
        if args.reset:
            uart.rts = True
            time.sleep(.15)
            uart.rts = False
        start = time.monotonic()
        sent = not bool(args.command)
        with args.log.open('wb') as log:
            while time.monotonic() - start < args.seconds:
                if not sent and time.monotonic() - start >= (3 if args.reset else .2):
                    uart.write(('\r\n' + args.command + '\r\n').encode())
                    sent = True
                data = uart.read(uart.in_waiting or 1)
                if data:
                    log.write(data)
                    log.flush()
                    print(data.decode('utf-8', 'replace'), end='', flush=True)
                time.sleep(.01)
    finally:
        uart.close()


if __name__ == '__main__':
    main()
