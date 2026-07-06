#!/usr/bin/env python3
"""Minimal AWS IoT MQTT-over-mutual-TLS subscriber using ONLY the Python stdlib (ssl + socket) -
no paho, no pip. Subscribes to bl602/# with the device certificate (a different client id, which the
demo policy allows) and prints each PUBLISH it receives - independent cloud-side proof that the
bl602 telemetry reaches AWS IoT Core. Usage: mqtt_sub.py <creds_dir> [seconds]."""
import os, ssl, socket, struct, sys, time

creds = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build_bl602", "aws")
run_s = int(sys.argv[2]) if len(sys.argv) > 2 else 40
host = open(os.path.join(creds, "endpoint.txt")).read().strip()
CLIENT_ID = "bl602-host-monitor"
TOPIC = "bl602/#"


def enc_len(n):                       # MQTT remaining-length varint
    out = b""
    while True:
        b = n & 0x7F; n >>= 7
        out += bytes([b | (0x80 if n else 0)])
        if not n: return out


def mqtt_str(s):
    b = s.encode(); return struct.pack("!H", len(b)) + b


ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.minimum_version = ssl.TLSVersion.TLSv1_2      # AWS IoT :8883 resets Python's TLS1.3 mutual-auth CH
ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ctx.load_verify_locations(os.path.join(creds, "ca.pem"))
ctx.load_cert_chain(os.path.join(creds, "cert.pem"), os.path.join(creds, "key.pem"))
raw = socket.create_connection((host, 8883), timeout=15)
tls = ctx.wrap_socket(raw, server_hostname=host)
print(f"[sub] TLS connected to {host}:8883")

# CONNECT (clean session, keepalive 60)
vh = mqtt_str("MQTT") + bytes([0x04, 0x02]) + struct.pack("!H", 60)
payload = mqtt_str(CLIENT_ID)
tls.sendall(bytes([0x10]) + enc_len(len(vh + payload)) + vh + payload)

# SUBSCRIBE bl602/# QoS0, packet id 1
sub = struct.pack("!H", 1) + mqtt_str(TOPIC) + bytes([0x00])
tls.sendall(bytes([0x82]) + enc_len(len(sub)) + sub)
print(f"[sub] subscribed to {TOPIC}; listening {run_s}s ...")

tls.settimeout(2.0)
buf = b""; deadline = time.time() + run_s; n_msgs = 0


def read_pkt(sock, buf):
    while len(buf) < 2:
        buf += sock.recv(4096)
    # decode remaining length
    mult = 1; val = 0; i = 1
    while True:
        while len(buf) <= i:
            buf += sock.recv(4096)
        b = buf[i]; val += (b & 0x7F) * mult; mult <<= 7; i += 1
        if not (b & 0x80): break
    total = i + val
    while len(buf) < total:
        buf += sock.recv(4096)
    return buf[0], buf[i:total], buf[total:]


while time.time() < deadline:
    try:
        typ, body, buf = read_pkt(tls, buf)
    except socket.timeout:
        continue
    if typ == 0x20:
        print("[sub] CONNACK", "OK" if body[1] == 0 else f"rc={body[1]}")
    elif typ == 0x90:
        print("[sub] SUBACK ok")
    elif (typ & 0xF0) == 0x30:                          # PUBLISH
        tlen = struct.unpack("!H", body[:2])[0]
        topic = body[2:2 + tlen].decode(errors="replace")
        pl = body[2 + tlen:].decode(errors="replace")
        n_msgs += 1
        print(f"[sub] <<< {topic}  {pl}")

print(f"[sub] done - received {n_msgs} message(s) from AWS IoT Core")
tls.close()
