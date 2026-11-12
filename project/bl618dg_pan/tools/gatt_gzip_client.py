#!/usr/bin/env python3
"""Receive MCU gzip/zstd/LZ4 over BR/EDR; verify synthetic UART payload."""
import argparse
import ctypes
import ctypes.util
import json
import select
import struct
import time
import zlib
from gatt_br_client import ATT


def source(offset, length, profile):
    if profile == 1:
        random = 0x9e3779b9 ^ (offset // 4096)
        out = bytearray()
        for _ in range(length):
            random ^= (random << 13) & 0xffffffff
            random ^= random >> 17
            random ^= (random << 5) & 0xffffffff
            out.append(random & 255)
        return bytes(out)
    lines = []
    for at in range(0, length, 64):
        n = (offset + at) // 64
        line = (f"t={n:08x} lvl=INFO rx={(n*37)&0xffffffff:08x} "
                f"tx={(n*11)&0xffffffff:08x} adc={n%4096:04x} state={n%4:02x}")
        lines.append(line.ljust(62).encode() + b"\r\n")
    return b"".join(lines)[:length]


class GzipATT(ATT):
    def __init__(self, address, profile, codec="gzip"):
        self.buffer = bytearray()
        self.raw_bytes = self.blocks = self.fallback = 0
        self.ended = False
        self.profile = profile
        self.codec = {"gzip": 1, "zstd": 2, "lz4": 3}[codec]
        self.library = None
        if codec != "gzip":
            name = ctypes.util.find_library(codec)
            if not name:
                raise RuntimeError(f"Missing host decoder library: {codec}")
            self.library = ctypes.CDLL(name)
            fn = self.library.ZSTD_decompress if codec == "zstd" else self.library.LZ4_decompress_safe
            if codec == "zstd":
                fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
                fn.restype = ctypes.c_size_t
            else:
                fn.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
                fn.restype = ctypes.c_int
        super().__init__(address, True)

    def decompress(self, payload, codec, raw_len):
        if codec == 0:
            return payload
        if codec == 1:
            return zlib.decompress(payload, 31)
        output = ctypes.create_string_buffer(raw_len)
        if codec == 2:
            count = self.library.ZSTD_decompress(output, raw_len, payload, len(payload))
        else:
            count = self.library.LZ4_decompress_safe(payload, output, len(payload), raw_len)
        if count != raw_len:
            raise ValueError(f"Codec {codec} decompression error / size mismatch: {count}")
        return output.raw

    def receive(self):
        packet = self.sock.recv(2048)
        if not packet:
            raise ConnectionError("ATT channel closed")
        if packet[0] == 0x1b:
            self.notifications += len(packet)-3
            self.buffer.extend(packet[3:])
            while len(self.buffer) >= 24:
                magic, seq, offset, raw_len, packed_len, codec = struct.unpack_from("<4s5I", self.buffer)
                if magic == b"GZE1" and seq == self.blocks and offset == self.raw_bytes:
                    if raw_len or packed_len or codec:
                        raise ValueError("Invalid end marker")
                    del self.buffer[:24]
                    self.ended = True
                    continue
                if magic != b"GZB1" or seq != self.blocks or offset != self.raw_bytes:
                    raise ValueError("Record sequence/offset mismatch")
                if not 0 < raw_len <= 4096 or not 0 < packed_len <= 4224 or codec not in (0, self.codec):
                    raise ValueError("Invalid record length/codec")
                if len(self.buffer) < 24 + packed_len:
                    break
                payload = bytes(self.buffer[24:24+packed_len])
                del self.buffer[:24+packed_len]
                decoded = self.decompress(payload, codec, raw_len)
                expected = source(offset, raw_len, self.profile)
                if decoded != expected:
                    raise ValueError("Decompressed source mismatch")
                self.raw_bytes += len(decoded)
                self.blocks += 1
                self.fallback += int(codec == 0)
        return packet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address")
    parser.add_argument("--seconds", type=int, default=15, choices=range(1, 61))
    parser.add_argument("--profile", choices=("text", "random"), default="text")
    parser.add_argument("--baud", type=int, default=0, help="8N1 synthetic arrivals; 0=capacity")
    parser.add_argument("--level", type=int, choices=range(1, 7), default=1)
    parser.add_argument("--codec", choices=("gzip", "zstd", "lz4"), default="gzip")
    parser.add_argument("--backlog-limit", type=int, default=65536)
    args = parser.parse_args()
    kind = int(args.profile == "random")
    att = GzipATT(args.address, kind, args.codec)
    try:
        handles = att.discover()
        print(json.dumps({"transport": "BR/EDR", "psm": 31,
                          "security": list(att.sock.getsockopt(274, 4, 2)),
                          "profile": args.profile, "baud": args.baud,
                          "codec": args.codec, "level": args.level, "seconds": args.seconds}), flush=True)
        att.write(handles[0x2902], b"\x01\x00")
        started = time.monotonic()
        att.write(handles[0xfff3], f"GZ {args.seconds} {kind} {args.baud} {args.level} {att.codec}".encode())
        keys = ("running", "raw_bytes", "wire_bytes", "peak_raw_backlog", "codec_call_wall_us",
                "errors", "producer_elapsed_ms", "baud", "profile", "level",
                "heap_start", "heap_low", "pending", "codec", "codec_workspace_peak")
        next_poll = started
        status = {}
        while time.monotonic()-started < args.seconds+15:
            if select.select([att.sock], [], [], 0.05)[0]:
                att.receive()
            if (att.ended or time.monotonic()-started > args.seconds+7) and time.monotonic() >= next_poll:
                raw_status = att.read(handles[0xfff3])
                if len(raw_status) not in (56, 64):
                    raise ValueError("Unexpected compression status length")
                values = struct.unpack("<4s"+"I"*((len(raw_status)-4)//4), raw_status)
                if values[0] != b"GZS1":
                    raise ValueError("Unexpected gzip status")
                status = dict(zip(keys, values[1:]))
                next_poll = time.monotonic()+0.25
                if not status["running"] and not status["pending"] and status["wire_bytes"] == att.notifications:
                    break
        elapsed = time.monotonic()-started
        target = args.baud*args.seconds//10 if args.baud else None
        valid = (bool(status) and att.ended and not status["running"] and not status["pending"]
                 and not status["errors"] and not att.buffer
                 and status["raw_bytes"] == att.raw_bytes
                 and status["wire_bytes"] == att.notifications
                 and (target is None or target == att.raw_bytes))
        realtime = valid and (target is None or
                             status["peak_raw_backlog"] <= args.backlog_limit)
        print(json.dumps({"verified": valid, "within_backlog_budget": realtime,
                          "received_raw": att.raw_bytes, "received_wire": att.notifications,
                          "blocks": att.blocks, "raw_fallback_blocks": att.fallback,
                          "wire_fraction": att.notifications/max(1, att.raw_bytes),
                          "elapsed_with_drain": elapsed,
                          "producer_drain_ms": max(0, status.get("producer_elapsed_ms", 0)-args.seconds*1000),
                          "equivalent_8n1_baud_including_drain": att.raw_bytes*10/elapsed,
                          "raw_Bps_including_drain": att.raw_bytes/elapsed,
                          "server": status}), flush=True)
        if not valid:
            raise RuntimeError("Source/count/completion verification failed")
    finally:
        att.sock.close()


if __name__ == "__main__":
    main()
