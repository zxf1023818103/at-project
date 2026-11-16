#!/usr/bin/env python3
"""Keep hostap symbols separate from the radio vendor's WPA implementation."""
import pathlib
import subprocess
import sys
import tempfile

nm, objcopy, source, output = sys.argv[1:]
symbols = set()
for line in subprocess.check_output([nm, '-g', '--defined-only', source], text=True).splitlines():
    words = line.split()
    if len(words) >= 3 and not words[-1].startswith('green2_'):
        symbols.add(words[-1])
with tempfile.TemporaryDirectory() as tmp:
    mapping = pathlib.Path(tmp) / 'symbols'
    mapping.write_text(''.join(f'{s} green2_pasn_{s}\n' for s in sorted(symbols)))
    subprocess.run([objcopy, '--redefine-syms=' + str(mapping), source, output], check=True)
