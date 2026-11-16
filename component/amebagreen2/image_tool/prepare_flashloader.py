#!/usr/bin/env python3
"""Copy ImageTool to build output and sign its RAM flashloader for this board."""
import argparse
from pathlib import Path
import shutil
from build_image import ROOT, IMAGE_TOOLS, PROJECT, axf
from green2_security import prepare, verify_manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keys', default=str(ROOT/'project/amebagreen2/keys/com15.json'))
    parser.add_argument('--output', default=str(ROOT/'build/amebagreen2/security-tools'))
    args = parser.parse_args()
    if not Path(args.keys).is_file():
        parser.error('Existing device keys are required; refusing to generate replacement keys')
    source = IMAGE_TOOLS/'Flash'
    out = Path(args.output).resolve()
    if out == source.resolve() or source.resolve() in out.parents:
        parser.error('Use a separate build output; never modify the vendored flashloader')
    shutil.copytree(source,out,dirs_exist_ok=True,ignore=shutil.ignore_patterns('log','Log','Logs'))
    loader = out/'Devices/Floaders/floader_amebagreen2.bin'
    original = loader.read_bytes()
    if len(original)<8192 or original[-4096:-4088] != bytes.fromhex('999996963fcc66fc'):
        raise ValueError('Unexpected SDK flashloader manifest')
    raw = original[:-4096]
    unsigned = out/'floader_unsigned.bin'
    unsigned.write_bytes(raw)
    config = prepare(PROJECT/'manifest.json5',out/'security',args.keys,True,False)
    axf('fw_pack','--imgtool-floader',unsigned,'-o',loader,extern=out/'security')
    signed = loader.read_bytes()
    verify_manifest(signed[-4096:],raw,bytes.fromhex(config['image1']['sboot_public_key']))
    print('Signed RAM flashloader: independent signature/hash verification PASS')


if __name__ == '__main__':
    main()
