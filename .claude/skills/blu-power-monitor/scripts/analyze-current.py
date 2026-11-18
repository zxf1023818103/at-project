#!/usr/bin/env python3
"""Offline current conversion using the user's local BLU SDK, without serial I/O."""
import argparse
import json
import statistics
import struct
import types
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--warmup-seconds', type=float, default=1.0)
    parser.add_argument('--voltage-mv', type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not 0 <= args.warmup_seconds < float('inf'):
        parser.error('warmup-seconds must be finite and nonnegative')
    if args.voltage_mv is not None and not 500 <= args.voltage_mv <= 5000:
        parser.error('voltage-mv must be in SDK range 500..5000')
    metadata = args.metadata.read_text(encoding='utf-8-sig')
    if 'END' not in metadata:
        parser.error('metadata is incomplete (missing END)')
    raw = args.raw.read_bytes()
    usable = len(raw) // 4 * 4
    warmup = int(args.warmup_seconds * 100000)
    if usable // 4 <= warmup:
        parser.error('not enough complete samples after warmup')
    invalid = sum(((v[0] >> 14) & 7) > 5
                  for v in struct.iter_unpack('<I', raw[:usable]))
    if invalid:
        parser.error(f'{invalid} samples contain invalid range bits; inspect stream alignment')
    # Execute only a trusted, user-provided local vendor SDK.
    with zipfile.ZipFile(args.sdk) as archive:
        source = archive.read('src/blu_all_in_one.py').decode('utf-8-sig')
    sdk = types.ModuleType('blu_offline_sdk')
    exec(compile(source, str(args.sdk), 'exec'), sdk.__dict__)

    class OfflineSerial:
        def __init__(self, *unused, **kwargs):
            pass

        def reset_input_buffer(self):
            pass

        def close(self):
            pass

    original = sdk.serial.Serial
    sdk.serial.Serial = OfflineSerial
    try:
        device = sdk.BLU2_API('OFFLINE')
    finally:
        sdk.serial.Serial = original
    # Require every SDK-used R/O parameter rather than silently using defaults.
    keys = {line.replace('=', ':').split(':', 1)[0].strip()
            for line in metadata.splitlines()}
    missing = sorted({f'{key}{i}' for key in ('R', 'O') for i in range(6)} - keys)
    if missing:
        parser.error('missing calibration fields: ' + ', '.join(missing))
    if not device._parse_metadata(metadata):
        parser.error('SDK could not parse metadata')
    samples, _ = device.get_samples(raw[:usable])
    stable = samples[warmup:]
    result = {
        'sample_count': len(samples), 'sample_rate_hz': 100000,
        'sample_duration_s': len(samples) / 100000,
        'ignored_tail_bytes': len(raw) - usable,
        'discarded_warmup_s': warmup / 100000,
        'statistics_duration_s': len(stable) / 100000,
        'mean_uA': statistics.fmean(stable),
        'min_uA': min(stable), 'max_uA': max(stable),
        'calibrated_flag': device.modifiers['Calibrated'],
        'conversion': 'vendor SDK get_samples; includes SDK range-switch filtering',
    }
    if args.voltage_mv is not None:
        result['assumed_voltage_mv'] = args.voltage_mv
        result['calculated_mean_power_mW'] = result['mean_uA'] * args.voltage_mv / 1e6
    output = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(output + '\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
