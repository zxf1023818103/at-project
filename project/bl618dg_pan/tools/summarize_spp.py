"""Validate SPP receiver counters and summarize artifacts from spp_load.ps1."""
import base64
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'build/validation/spp'
rows = []
for path in sorted(root.glob('*.json')):
    if not re.fullmatch(r'(tx|rx)-(?:[0-9]+|smoke)\.json', path.name):
        continue
    result = json.loads(path.read_text(encoding='utf-8-sig'))
    uart = base64.b64decode(result.pop('uart_b64')).decode()
    path.with_suffix('.uart.log').write_text(uart)
    match = re.search(r'RESULT mode=(tx|rx) bytes=(\d+) elapsed_ms=(\d+) bad=(\d+) blocked=(\d+) errors=(\d+) connected=(\d+)(?: rx_span_ms=(\d+))?', uart)
    if not match:
        raise ValueError(f'{path}: missing board result')
    board_mode, count, elapsed, bad, blocked, errors, connected, span = match.groups()
    assert board_mode == result['mode'], path
    assert int(count) == result['bytes'] > 0, path
    assert int(bad) == int(errors) == result['bad_bytes'] == 0, path
    assert connected == '1', path
    receiver_mbps = result['mbps']
    if board_mode == 'rx':
        receiver_mbps = int(count) * 8 / int(span) / 1000 if span and int(span) else None
    row = dict(file=path.name, mode=board_mode, bytes=int(count), receiver_mbps=receiver_mbps,
               host_mbps=result['mbps'], board_elapsed_ms=int(elapsed), rx_span_ms=int(span or 0),
               bad_bytes=int(bad), errors=int(errors), blocked=int(blocked), passed=True,
               congestion_diagnostics=uart.count('l2cab is_cong_cback_context'),
               continuation_diagnostics=uart.count('continuation for unknown packet'))
    rows.append(row)
    print(json.dumps(row))
(root / 'summary.json').write_text(json.dumps(rows, indent=2) + '\n')
