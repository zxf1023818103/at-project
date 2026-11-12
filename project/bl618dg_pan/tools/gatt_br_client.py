#!/usr/bin/env python3
"""Linux ATT client using the BR/EDR L2CAP PSM (31), never LE fixed CID."""
import argparse
import json
import select
import socket
import struct
import time


class ATT:
    def __init__(self, address, secure):
        self.sock = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET,
                                  socket.BTPROTO_L2CAP)
        self.sock.settimeout(15)
        if secure:
            # BT_SECURITY_MEDIUM requires encryption on the BR/EDR ACL.
            self.sock.setsockopt(274, 4, bytes([2, 0]))
        self.sock.connect((address, 31))
        self.notifications = 0
        self.bad_bytes = 0

    def receive(self):
        packet = self.sock.recv(2048)
        if not packet:
            raise ConnectionError("ATT channel closed")
        if packet[0] == 0x1b:
            payload = packet[3:]
            self.bad_bytes += sum(v != (self.notifications + i) % 251
                                  for i, v in enumerate(payload))
            self.notifications += len(payload)
        return packet

    def exchange(self, packet, response):
        self.sock.sendall(packet)
        while True:
            reply = self.receive()
            if reply[0] == 1:
                raise RuntimeError("ATT error: " + reply.hex())
            if reply[0] == response:
                return reply
            if reply[0] != 0x1b:
                raise RuntimeError("Unexpected ATT PDU: " + reply.hex())

    def write(self, handle, value, response=True):
        packet = struct.pack("<BH", 0x12 if response else 0x52, handle) + value
        if response:
            self.exchange(packet, 0x13)
        else:
            self.sock.sendall(packet)

    def read(self, handle):
        return self.exchange(struct.pack("<BH", 0x0a, handle), 0x0b)[1:]

    def discover(self):
        start, end = 1, 0xffff
        while True:
            p = self.exchange(struct.pack("<BHHH", 0x10, start, 0xffff, 0x2800), 0x11)
            length = p[1]
            for offset in range(2, len(p), length):
                entry = p[offset:offset+length]
                first, last = struct.unpack_from("<HH", entry)
                if length == 6 and entry[4:6] == b"\xf0\xff":
                    start, end = first, last
                    break
            else:
                start = last + 1
                continue
            break
        handles = {}
        cursor = start
        while cursor <= end:
            try:
                p = self.exchange(struct.pack("<BHHH", 8, cursor, end, 0x2803), 9)
            except RuntimeError as exc:
                if str(exc).endswith("0a"):
                    break
                raise
            length = p[1]
            for offset in range(2, len(p), length):
                declaration, props, value, uuid = struct.unpack_from("<HBHH", p, offset)
                handles[uuid] = value
                cursor = declaration + 1
        # The fixture places CCCD immediately after the notification value.
        handles[0x2902] = handles[0xfff2] + 1
        p = self.exchange(struct.pack("<BHH", 4, handles[0x2902], handles[0x2902]), 5)
        if p[1] != 1 or p[4:6] != b"\x02\x29":
            raise RuntimeError("Missing notification CCCD")
        return handles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--seconds", type=int, default=10, choices=range(1, 61))
    parser.add_argument("--block", type=int, default=244, choices=range(1, 513))
    parser.add_argument("--secure", action="store_true", help="require encrypted BR/EDR")
    args = parser.parse_args()
    att = ATT(args.address, args.secure)
    try:
        handles = att.discover()
        print(json.dumps({"transport": "BR/EDR", "psm": 31, "handles": handles,
                          "security": list(att.sock.getsockopt(274, 4, 2)),
                          "greeting": att.read(handles[0xfff1]).decode()}), flush=True)
        ctrl, rx, tx = handles[0xfff3], handles[0xfff1], handles[0xfff2]
        att.write(handles[0x2902], b"\x01\x00")
        att.write(ctrl, f"TX {args.seconds} {args.block}".encode())
        started = time.monotonic()
        while time.monotonic()-started < args.seconds+1:
            if select.select([att.sock], [], [], 0.2)[0]:
                att.receive()
        status = struct.unpack("<4sBBH6I", att.read(ctrl))
        if status[2] != 1:
            raise RuntimeError("Server did not report BR/EDR transport")
        tx_result = {"direction": "board_to_linux", "seconds": args.seconds,
                     "elapsed_with_drain": time.monotonic()-started,
                     "received": att.notifications, "server_queued": status[4],
                     "bad_bytes": att.bad_bytes, "server_errors": status[6],
                     "mbps": att.notifications*8/args.seconds/1e6}
        print(json.dumps(tx_result), flush=True)
        if att.notifications != status[4] or att.bad_bytes or status[6]:
            raise RuntimeError("Notification integrity/count mismatch")
        att.write(ctrl, f"RX {args.seconds+3} {args.block}".encode())
        sent = 0
        started = time.monotonic()
        while time.monotonic()-started < args.seconds:
            value = bytes((sent+i) % 251 for i in range(args.block))
            att.write(rx, value, response=False)
            sent += len(value)
        # Ordered request is a fence for preceding write commands on this channel.
        att.write(ctrl, b"STOP")
        elapsed = time.monotonic()-started
        status = struct.unpack("<4sBBH6I", att.read(ctrl))
        print(json.dumps({"direction": "linux_to_board", "sent": sent,
                          "received": status[5], "bad_bytes": status[7],
                          "elapsed": elapsed, "mbps": status[5]*8/elapsed/1e6}), flush=True)
        if sent != status[5] or status[7]:
            raise RuntimeError("Write integrity/count mismatch")
    finally:
        att.sock.close()


if __name__ == "__main__":
    main()
