#!/usr/bin/env python3
"""Run the installed Flash Studio iPhone CoC benchmark through an authorized Mac.

SSH keys are preferred. If SSHPASS is set, sshpass -e is used; no credential is
written into reports. Each direction runs up to 60 seconds. Results count only
receiver-confirmed payload, including ACK/drain time, with matching byte counts.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import uuid


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mac', required=True, help='user@host')
    ap.add_argument('--device', required=True, help='devicectl iPhone identifier')
    ap.add_argument('--bundle', default='cn.zenghome.flash-studio-nan')
    ap.add_argument('--stack', choices=['Bluedroid', 'NimBLE'], required=True)
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--psm', type=lambda s: int(s, 0), help='known PSM; bypass GATT discovery in the standalone bench')
    ap.add_argument('--known-hosts', help='optional task-specific SSH known_hosts file')
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if not 1 <= args.seconds <= 60:
        ap.error('--seconds must be between 1 and 60')
    if args.psm is not None and not 0x80 <= args.psm <= 0xff:
        ap.error('--psm must be in 0x80..0xff')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    ssh = ['ssh', '-o', 'ConnectTimeout=8', '-o', 'StrictHostKeyChecking=accept-new']
    if args.known_hosts:
        ssh += ['-o', 'UserKnownHostsFile=' + args.known_hosts]
    if os.environ.get('SSHPASS'):
        ssh = ['sshpass', '-e'] + ssh
    ssh += [args.mac]

    def remote(command):
        result = subprocess.run(ssh + [command], capture_output=True, text=True, timeout=45)
        if result.returncode:
            raise RuntimeError("Remote command failed: " + result.stderr.strip())
        return result.stdout

    def call(arguments):
        return shlex.join(['xcrun', 'devicectl'] + arguments)

    start = datetime.now(timezone.utc)
    launch = ['device', 'process', 'launch', '--device', args.device, '--terminate-existing',
              args.bundle, '--l2cap', '--autotest', '--seconds=' + str(args.seconds)]
    launch += ['--bluedroid-coc' if args.stack == 'Bluedroid' else '--nimble-coc']
    if args.psm is not None:
        launch += ['--coc-psm=' + str(args.psm)]
    remote(call(launch))
    temporary = '/tmp/flash-studio-coc-' + uuid.uuid4().hex + '.json'
    copy = call(['device', 'copy', 'from', '--device', args.device, '--domain-type',
                 'appDataContainer', '--domain-identifier', args.bundle, '--source',
                 'Documents/l2cap-throughput.json', '--destination', temporary])
    deadline = time.monotonic() + 2 * args.seconds + 100
    report = {}
    try:
        while time.monotonic() < deadline:
            time.sleep(8)
            report = json.loads(remote(copy + ' >/dev/null && cat ' + shlex.quote(temporary)))
            updated = datetime.fromisoformat(report['updated'].replace('Z', '+00:00'))
            if (updated - start).total_seconds() < -1:
                continue  # old report, not proof that this launch ran
            report['runner'] = {'stack': args.stack, 'device': args.device, 'bundle': args.bundle,
                                'started': start.isoformat(), 'seconds_per_direction': args.seconds,
                                'psm_source': 'explicit' if args.psm is not None else 'gatt',
                                'psm_override': args.psm}
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
            live = report.get('live', {})
            print(f"{report.get('host_stack')} {report.get('phase')}: "
                  f"sent={live.get('submitted')} received={live.get('received')} "
                  f"bad={live.get('bad_bytes')}", flush=True)
            if report.get('failure'):
                raise RuntimeError(report['failure'])
            results = report.get('results', [])
            if not report.get('running') and len(results) == 2:
                assert report.get('host_stack') == args.stack, 'wrong Host selected'
                assert {r['direction'] for r in results} == {'iphone_to_board', 'board_to_iphone'}
                for result in results:
                    assert result['passed'] and result['bad_bytes'] == result['peer_errors'] == 0
                    assert result['host_bytes'] == result['peer_bytes'] > 0
                    assert result['configured_seconds'] == args.seconds
                    print(f"PASS {result['direction']}: {result['payload_mbps']:.6f} Mbps, "
                          f"{result['peer_bytes']} bytes", flush=True)
                return
        raise TimeoutError('No completed, receiver-confirmed bidirectional result')
    finally:
        try:
            remote('rm -f ' + shlex.quote(temporary))
        except subprocess.SubprocessError:
            pass


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, RuntimeError, TimeoutError, subprocess.SubprocessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        sys.exit(1)
