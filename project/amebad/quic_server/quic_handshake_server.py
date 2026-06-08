#!/usr/bin/env python3
"""
Minimal QUIC handshake server for the AmebaD ngtcp2 + wolfSSL client self-test.

Listens on UDP 4433 (all interfaces), advertises ALPN "hq-interop", and logs
every QUIC handshake completion / teardown. It does no application protocol —
the device-side test (component/amebad/app_km4/quic_test.c) only proves the QUIC
v1 + TLS 1.3 handshake, then sends CONNECTION_CLOSE.

Run (after `sudo apt install -y python3-aioquic`):
    python3 quic_handshake_server.py
Self-signed cert/key (cert.pem/key.pem) are generated alongside this script.
"""
import argparse
import asyncio

from aioquic.asyncio import serve
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import HandshakeCompleted, ConnectionTerminated, PingAcknowledged


class HandshakeLogger(QuicConnectionProtocol):
    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            print(f"[srv] ===> HANDSHAKE COMPLETE  alpn={event.alpn_protocol!r} "
                  f"early_data={event.early_data_accepted}", flush=True)
        elif isinstance(event, ConnectionTerminated):
            print(f"[srv] connection terminated  error_code={event.error_code} "
                  f"frame_type={event.frame_type} reason={event.reason_phrase!r}",
                  flush=True)
        elif isinstance(event, PingAcknowledged):
            print("[srv] ping acked", flush=True)


async def main(host: str, port: int, cert: str, key: str, alpn: str):
    cfg = QuicConfiguration(is_client=False, alpn_protocols=[alpn])
    cfg.load_cert_chain(cert, key)
    print(f"[srv] QUIC server on {host}:{port}  ALPN={alpn!r}  (Ctrl-C to stop)",
          flush=True)
    await serve(host, port, configuration=cfg, create_protocol=HandshakeLogger)
    await asyncio.Future()  # run forever


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=4433)
    ap.add_argument("--cert", default="cert.pem")
    ap.add_argument("--key", default="key.pem")
    ap.add_argument("--alpn", default="hq-interop")
    args = ap.parse_args()
    try:
        asyncio.run(main(args.host, args.port, args.cert, args.key, args.alpn))
    except KeyboardInterrupt:
        print("\n[srv] stopped")
