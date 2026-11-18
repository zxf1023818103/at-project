#!/usr/bin/env python3
"""Create a standalone CoC Bench project from Flash Studio's exact transfer source.

The separate bundle uses ordinary Bluetooth permission, without changing the
existing CS/AccessorySetupKit app. Requires the local flash-studio-ios checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--flash-studio', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--bundle', default='cn.zenghome.flash-studio.cocbench')
a = p.parse_args()
source = a.flash_studio / 'L2CAPBench.swift'
if not source.is_file():
    p.error('L2CAPBench.swift is missing')
if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+){2,}', a.bundle):
    p.error('invalid bundle identifier')
if a.bundle in ('cn.zenghome.flash-studio', 'cn.zenghome.flash-studio-nan'):
    p.error('choose a separate identifier; do not replace an existing Flash Studio app')
if a.output.exists():
    p.error('output must be a new directory')
shutil.copytree(Path(__file__).with_name('ios'), a.output)
# Select one service explicitly when testing two Hosts on the same bonded
# identity; CoreBluetooth can retain both service UUIDs in its cache.
content = source.read_text()
old = 'ProcessInfo.processInfo.arguments.contains("--bluedroid-coc") ? [bluedroidService] : [bluedroidService, nimbleService]'
new = 'ProcessInfo.processInfo.arguments.contains("--nimble-coc") ? [nimbleService] : (' + old + ')'
if content.count(old) != 1:
    p.error('review the Flash Studio service selection before generating the bench')
content = content.replace(old, new)
old_connect = '        status = "读取 CoC PSM…"; p.discoverServices(services)'
new_connect = '        if let argument = ProcessInfo.processInfo.arguments.first(where: { $0.hasPrefix("--coc-psm=") }),\n           let psm = UInt16(argument.dropFirst(10)), (0x80...0xff).contains(psm) {\n            hostStack = ProcessInfo.processInfo.arguments.contains("--bluedroid-coc") ? "Bluedroid" : "NimBLE"\n            status = "打开已知 LE CoC PSM \\(psm)…"; p.openL2CAPChannel(psm); return\n        }\n' + old_connect
if content.count(old_connect) != 1:
    p.error('review didConnect before adding the explicit PSM benchmark option')
content = content.replace(old_connect, new_connect)

(a.output / source.name).write_text(content)
project = a.output / 'CoCBench.xcodeproj/project.pbxproj'
project.write_text(project.read_text().replace('cn.zenghome.flash-studio.cocbench', a.bundle))
(a.output / 'source.json').write_text(json.dumps({
    'source': str(source.resolve()), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'bundle': a.bundle, 'protocol_source_modified': False,
    'discovery_modified': 'explicit --nimble-coc filter and optional --coc-psm=N',
    'generated_sha256': hashlib.sha256(content.encode()).hexdigest()
}, indent=2) + '\n')
print(a.output)
