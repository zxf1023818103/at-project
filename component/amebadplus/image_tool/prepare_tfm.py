"""Create TF-M's private patched mbedTLS tree without changing shared submodules."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
source = a.root / 'component/mbedtls_config/mbedtls'
patches = a.root / 'component/trusted-firmware-m_config/trusted-firmware-m/lib/ext/mbedcrypto'
selected_patches = [p for p in sorted(patches.glob('*.patch')) if 'CC3XX' not in p.name]
digest = hashlib.sha256()
for path in sorted(source.rglob('*')):
    relative = path.relative_to(source)
    if any(part in ('.git', '__pycache__') for part in relative.parts) or not path.is_file():
        continue
    digest.update(str(relative).encode())
    digest.update(path.read_bytes())
for path in selected_patches:
    digest.update(path.name.encode())
    digest.update(path.read_bytes())
fingerprint = digest.hexdigest()
stamp = a.output / '.dplus-source-sha256'
if not stamp.exists() or stamp.read_text().strip() != fingerprint:
    if a.output.exists():
        shutil.rmtree(a.output)
    shutil.copytree(source, a.output, ignore=shutil.ignore_patterns('.git', '__pycache__'))
for patch in selected_patches:
    def check(reverse=False):
        return subprocess.run(['patch', '-p1', '--batch', '--dry-run',
            '--reverse' if reverse else '--forward', '-i', str(patch)],
            cwd=a.output, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if check():
        subprocess.run(['patch', '-p1', '--batch', '--forward', '-i', str(patch)], cwd=a.output, check=True)
    elif not check(True):
        raise RuntimeError(f'TF-M patch no longer applies: {patch.name}')
stamp.write_text(fingerprint + '\n')
