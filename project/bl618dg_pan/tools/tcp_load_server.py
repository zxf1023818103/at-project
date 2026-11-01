"""Local TCP test peer for net_load; emits one JSON result per connection.

Run on the Windows PAN/Wi-Fi peer. Bind only its test-interface addresses.
Example: python tcp_load_server.py --bind 192.168.100.6 --bind 192.168.138.1
"""
import argparse
import json
import socket
import select
import threading
import time


def transfer(conn, peer, local):
    total = 0
    mode = "unknown"
    started = None
    error = None
    end_reason = "duration"
    try:
        conn.settimeout(10)
        header = bytearray()
        while not header.endswith(b"\n") and len(header) < 40:
            chunk = conn.recv(1)
            if not chunk:
                raise ValueError("incomplete request")
            header.extend(chunk)
        mode, duration = header.decode("ascii").split()
        duration = int(duration)
        if mode not in ("RX", "TX") or not 1 <= duration <= 300:
            raise ValueError("invalid request")
        conn.settimeout(1 if mode == "RX" else duration + 10)
        started = time.monotonic()
        block = bytes([0x5A]) * 16384
        while time.monotonic() - started < duration + 5:
            if mode == "RX":
                if select.select([conn], [], [], 0)[0] and not conn.recv(1):
                    end_reason = "receiver_closed"
                    break
                try:
                    count = conn.send(block)
                except socket.timeout:
                    continue
            else:
                chunk = conn.recv(65536)
                if not chunk:
                    end_reason = "eof"
                    break
                if chunk != bytes([0x5A]) * len(chunk):
                    raise ValueError("payload mismatch")
                count = len(chunk)
            total += count
    except (OSError, ValueError) as exc:
        if (mode == "RX" and started is not None and
                time.monotonic() - started >= duration and
                isinstance(exc, (ConnectionResetError, BrokenPipeError))):
            end_reason = "receiver_closed"
        else:
            error = str(exc)
            end_reason = "error"
    finally:
        elapsed = time.monotonic() - started if started else 0
        conn.close()
        print(json.dumps({"local": local, "peer": peer, "device_direction": mode,
                          "bytes": total, "seconds": round(elapsed, 3),
                          "kbps": round(total * 8 / elapsed / 1000, 3) if elapsed else 0,
                          "end_reason": end_reason, "error": error}), flush=True)


def listen(address, port):
    with socket.socket() as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((address, port))
        server.listen(4)
        print(json.dumps({"listening": [address, port]}), flush=True)
        while True:
            conn, peer = server.accept()
            threading.Thread(target=transfer, args=(conn, peer, conn.getsockname()), daemon=True).start()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", action="append", required=True)
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()
    threads = [threading.Thread(target=listen, args=(address, args.port)) for address in args.bind]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
