#!/usr/bin/env python3
"""Extract a hash-pinned vendor function with its 32-bit return value intact.

The vendor function loads the schedule bitmap from action_entry+76 as u32,
but its final UXTB r0,r6 truncates 0x100 and above to zero. Both callers use
the full r0 bitmap, treating zero as allocation failure. This causes a false
RESOURCE_LIMITATION and leaves allocated actions outside the rollback bitmap.

Keep all original instructions and relocations except UXTB -> MOV. Link the
extracted function with --wrap; never modify the SDK's vendor archive.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import struct
import tempfile

EXPECTED = "b48e7eeeae14c8331e1979414cdfc1099262d88adf43f702c4d5be292574e385"
FUNCTION = "rtw_nan_schdl_faw_act_add"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ar")
    parser.add_argument("objcopy")
    parser.add_argument("archive")
    parser.add_argument("output")
    args = parser.parse_args()
    data = subprocess.check_output([args.ar, "p", args.archive,
                                    "rtw_nan_scheduler_faw.o"])
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise SystemExit("Unverified Green2 FAW object; refusing private ABI patch")
    # Pinned ELF: function section file offset 0x350, return at +0x166.
    offset = 0x350 + 0x166
    if data[offset:offset + 2] != bytes.fromhex("f0 b2"):
        raise SystemExit("Expected UXTB r0,r6 at the function return")
    patched = bytearray(data[:offset] + bytes.fromhex("30 46") + data[offset + 2:])
    # The duration helper shares this object. Its definition is omitted from
    # the extracted function, so leave an external reference to the original.
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, shoff + i * shentsize)
                for i in range(shnum)]
    shstr = sections[struct.unpack_from("<H", data, 50)[0]]
    section_names = data[shstr[4]:shstr[4] + shstr[5]]
    keep = {"", ".symtab", ".strtab", ".shstrtab", ".ARM.attributes",
            ".text." + FUNCTION, ".rel.text." + FUNCTION,
            ".rodata." + FUNCTION + ".str1.1",
            ".rodata.rtw_nan_schdl_faw_act_remove.str1.1"}
    remove = []
    for section in sections:
        name = section_names[section[0]:section_names.index(b"\0", section[0])].decode()
        if name not in keep:
            remove.append("--remove-section=" + name)
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for symoff in range(section[4], section[4] + section[5], section[9]):
            nameoff, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, symoff)
            name = names[nameoff:names.index(b"\0", nameoff)].decode()
            if name == "rtw_nan_schdl_get_duration_in_cmt_schdl":
                struct.pack_into("<IIIBBH", patched, symoff,
                                 nameoff, 0, 0, info, other, 0)
    with tempfile.TemporaryDirectory(prefix="green2-faw-") as tmp:
        source = Path(tmp) / "patched.o"
        source.write_bytes(patched)
        subprocess.run([
            args.objcopy,
            *remove,
            "--redefine-sym=" + FUNCTION + "=__wrap_" + FUNCTION,
            str(source), args.output,
        ], check=True)
    print("Green2 FAW schedule ID: pinned function extracted, UXTB -> MOV")


if __name__ == "__main__":
    main()
