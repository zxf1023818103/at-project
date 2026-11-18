#!/usr/bin/env python3
"""Redirect two cleanup calls in the hash-pinned native peer destructor.

Peer aging omits the selected NDC reference release before clearing its ID,
and final storage release omits the service/NDI mutex deletes. Patch the NDP
cleanup and final free relocations so internal retirement/rollback paths are
covered too. Native NDC reference guards, locking and queue teardown remain.
No instruction bytes or other relocations change. The input archive is intact.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess

EXPECTED = "df936272638fdd87c30150a0b8599e522e332f4e42040201b612e358ca7cb97a"
SECTION = ".rel.text.rtw_nan_peer_info_free"
PATCHES = {
    0x48: (10, "rtw_nan_ndp_data_free", "green2_nan_peer_ndp_free"),  # THM_CALL
    0x7a: (30, "rtos_mem_free", "green2_nan_peer_storage_free"),  # THM_JUMP24
}


def patch(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("Unverified Green2 peer object; refusing private ABI patch")
    result = bytearray(data)
    shoff = struct.unpack_from("<I", data, 32)[0]
    entsize, count, names_index = struct.unpack_from("<HHH", data, 46)
    sections = [list(struct.unpack_from("<10I", data, shoff + i * entsize))
                for i in range(count)]

    def contents(index):
        section = sections[index]
        return data[section[4]:section[4] + section[5]]

    def replace(index, payload):
        result.extend(b"\0" * (-len(result) % max(4, sections[index][8])))
        sections[index][4] = len(result)
        sections[index][5] = len(payload)
        result.extend(payload)

    names = contents(names_index)
    section_names = [names[s[0]:names.index(b"\0", s[0])].decode() for s in sections]
    rel_index = section_names.index(SECTION)
    symbol_index = sections[rel_index][6]
    string_index = sections[symbol_index][6]
    strings = contents(string_index)
    symbols = contents(symbol_index)
    relocations = bytearray(contents(rel_index))
    new_strings = bytearray(strings)
    new_symbols = bytearray(symbols)
    matches = set()
    for pos in range(0, len(relocations), 8):
        offset, info = struct.unpack_from("<II", relocations, pos)
        if offset not in PATCHES:
            continue
        relocation_type, original, gate = PATCHES[offset]
        name_offset = struct.unpack_from("<I", symbols, (info >> 8) * 16)[0]
        name = strings[name_offset:strings.index(b"\0", name_offset)].decode()
        if (info & 0xff) != relocation_type or name != original or offset in matches:
            raise ValueError(f"Expected peer destructor's {original} relocation at {offset:#x}")
        struct.pack_into("<II", relocations, pos, offset,
                         ((len(new_symbols) // 16) << 8) | relocation_type)
        new_symbols.extend(struct.pack("<IIIBBH", len(new_strings), 0, 0, 0x12, 0, 0))
        new_strings.extend(gate.encode() + b"\0")
        matches.add(offset)
    if matches != PATCHES.keys():
        raise ValueError("Expected both peer cleanup relocations")
    replace(string_index, new_strings)
    replace(symbol_index, new_symbols)
    replace(rel_index, relocations)
    for i, section in enumerate(sections):
        struct.pack_into("<10I", result, shoff + i * entsize, *section)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ar", "archive", "output"):
        parser.add_argument(name)
    args = parser.parse_args()
    original = subprocess.check_output([args.ar, "p", args.archive, "rtw_nan_peer.o"])
    Path(args.output).write_bytes(patch(original))
    print("Green2 peer teardown: release selected NDC before NDP clear, then both queue mutexes")


if __name__ == "__main__":
    main()
