#!/usr/bin/env python3
"""Verify SoftAP HTTP transfers through the RouterOS station/NAT fixture."""
import argparse
import hashlib
import http.client
import json
import pathlib
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.100.2')
    parser.add_argument('--port', type=int, default=18080)
    parser.add_argument('--rounds', type=int, default=30)
    parser.add_argument('--bytes', type=int, default=2*1024*1024)
    parser.add_argument('--min-mbps', type=float, default=0,
                        help='optional required payload rate (2 Mbaud 8N1 needs at least 1.6)')
    parser.add_argument('--reconnects', type=int, default=0)
    parser.add_argument('--aborts', type=int, default=0)
    parser.add_argument('--router', default='admin@192.168.100.2')
    parser.add_argument('--out', type=pathlib.Path, required=True)
    args = parser.parse_args()
    payload = (bytes(range(251))*((args.bytes+250)//251))[:args.bytes]
    digest = hashlib.sha256(payload).hexdigest()
    args.out.parent.mkdir(parents=True, exist_ok=True)

    def request(method, path, body=None, timeout=40):
        connection = http.client.HTTPConnection(args.host, args.port, timeout=timeout)
        try:
            connection.request(method, path, body=body)
            response = connection.getresponse()
            data = response.read()
            if response.status != 200:
                raise RuntimeError(f'HTTP {response.status}: {data[:200]!r}')
            return data
        finally:
            connection.close()

    with args.out.open('w', buffering=1) as log:
        def record(value):
            value['time'] = time.time()
            line = json.dumps(value)
            print(line, flush=True)
            log.write(line+'\n')

        record({'event': 'start', 'bytes': args.bytes, 'sha256': digest,
                'status': json.loads(request('GET', '/status'))})
        failures = 0
        for iteration in range(args.rounds):
            for direction in ('download', 'upload'):
                start = time.monotonic()
                try:
                    if direction == 'download':
                        data = request('GET', f'/file?bytes={args.bytes}')
                        if len(data) != args.bytes or hashlib.sha256(data).hexdigest() != digest:
                            raise RuntimeError('Download length/SHA256 mismatch')
                    else:
                        result = json.loads(request('POST', '/upload', payload))
                        if result != {'received': args.bytes, 'bad': 0}:
                            raise RuntimeError(f'Upload mismatch: {result}')
                    elapsed = time.monotonic()-start
                    mbps = args.bytes*8/elapsed/1e6
                    rate_ok = mbps >= args.min_mbps
                    if not rate_ok:
                        failures += 1
                    record({'event': 'transfer', 'round': iteration, 'direction': direction,
                            'seconds': elapsed, 'mbps': mbps, 'ok': True, 'rate_ok': rate_ok})
                except Exception as error:
                    failures += 1
                    record({'event': 'transfer', 'round': iteration, 'direction': direction,
                            'seconds': time.monotonic()-start, 'ok': False, 'error': str(error)})
            try:
                record({'event': 'status', 'round': iteration,
                        'status': json.loads(request('GET', '/status'))})
            except Exception as error:
                record({'event': 'status_error', 'error': str(error)})
                failures += 1
                break
        for iteration in range(args.reconnects):
            start = time.monotonic()
            try:
                def ros(command):
                    return subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=6',
                                           args.router, command], check=True, capture_output=True,
                                          text=True, timeout=20).stdout
                ros('/interface wifi disable [find name="wifi2"]')
                time.sleep(2)
                ros('/interface wifi enable [find name="wifi2"]')
                deadline = time.monotonic()+45
                while True:
                    try:
                        status = json.loads(request('GET', '/status', timeout=3))
                        break
                    except Exception:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(1)
                ready_seconds = time.monotonic()-start
                data = request('GET', f'/file?bytes={args.bytes}')
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError('Reconnect download SHA256 mismatch')
                result = json.loads(request('POST', '/upload', payload))
                if result != {'received': args.bytes, 'bad': 0}:
                    raise RuntimeError(f'Reconnect upload mismatch: {result}')
                record({'event': 'reconnect', 'round': iteration, 'ok': True,
                        'seconds': time.monotonic()-start, 'http_ready_seconds': ready_seconds,
                        'status': status})
            except Exception as error:
                failures += 1
                record({'event': 'reconnect', 'round': iteration, 'ok': False,
                        'seconds': time.monotonic()-start, 'error': str(error)})
                break
        for iteration in range(args.aborts):
            connection = http.client.HTTPConnection(args.host, args.port, timeout=10)
            start = time.monotonic()
            try:
                if iteration % 2 == 0:
                    connection.request('GET', '/file?bytes=33554432')
                    connection.getresponse().read(65536)
                else:
                    connection.putrequest('POST', '/upload')
                    connection.putheader('Content-Length', str(args.bytes))
                    connection.endheaders()
                    connection.send(payload[:65536])
                # Close deliberately while the declared body is incomplete.
                connection.close()
                status = json.loads(request('GET', '/status'))
                data = request('GET', f'/file?bytes={args.bytes}')
                if hashlib.sha256(data).hexdigest() != digest:
                    raise RuntimeError('After-abort download SHA256 mismatch')
                result = json.loads(request('POST', '/upload', payload))
                if result != {'received': args.bytes, 'bad': 0}:
                    raise RuntimeError(f'After-abort upload mismatch: {result}')
                record({'event': 'abort_recovery', 'round': iteration, 'ok': True,
                        'seconds': time.monotonic()-start, 'status': status})
            except Exception as error:
                failures += 1
                record({'event': 'abort_recovery', 'round': iteration, 'ok': False,
                        'error': str(error)})
                break
            finally:
                connection.close()
        record({'event': 'finish', 'failures': failures})
        raise SystemExit(bool(failures))


if __name__ == '__main__':
    main()
