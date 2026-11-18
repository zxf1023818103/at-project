#!/usr/bin/env python3
"""Extract pinned SDF RX, redirect only its plaintext destination check.

Instructions and other relocations are unchanged. The ARM adapter uses live
registers at +0x56, hence the entire object is pinned, not just the call bytes.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

EXPECTED = "c7ca2a0be60fb2870939233852aa3037ad7201b6aeec906769fd77077ce905e1"
FUNCTION = "rtw_nan_sdf_rx"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ar", "objcopy", "archive", "output"):
        parser.add_argument(name)
    args = parser.parse_args()
    data = subprocess.check_output([args.ar, "p", args.archive, "rtw_nan_disc_sdf.o"])
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise SystemExit("Unverified Green2 SDF object; refusing private ABI patch")
    patched = bytearray(data)
    shoff = struct.unpack_from("<I", data, 32)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, shoff + i * entsize)
                for i in range(count)]
    strings = sections[names_index]
    names = data[strings[4]:strings[4] + strings[5]]
    section_names = [names[s[0]:names.index(b"\0", s[0])].decode() for s in sections]
    keep = {"", ".symtab", ".strtab", ".shstrtab", ".ARM.attributes",
            ".text." + FUNCTION, ".rel.text." + FUNCTION}
    kept_indices = {i for i, name in enumerate(section_names) if name in keep}
    for section in sections:
        if section[1] != 2:
            continue
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, offset)
            if 0 < index < count and index not in kept_indices and info >> 4:
                # Retain external calls into the original archive, not duplicate
                # definitions from sections omitted by objcopy.
                struct.pack_into("<IIIBBH", patched, offset, name, 0, 0, info, other, 0)
    with tempfile.TemporaryDirectory(prefix="green2-bootstrap-") as tmp:
        source = Path(tmp) / "extracted.o"
        source.write_bytes(patched)
        subprocess.run([args.objcopy,
                        *["--remove-section=" + n for n in section_names if n not in keep],
                        "--redefine-sym=" + FUNCTION + "=__wrap_" + FUNCTION,
                        "--redefine-sym=memcmp=green2_nan_sdf_rx_gate",
                        str(source), args.output], check=True)
    print("Green2 bootstrap: pinned SDF RX extracted with bounded request gate")


if __name__ == "__main__":
    main()
