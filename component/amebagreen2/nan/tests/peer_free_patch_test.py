"""Check the derived vendor object's patch boundary using its pinned input."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("peer_patch", ROOT / "fixes/make_nan_peer_free_fix.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sections(data):
    offset = struct.unpack_from("<I", data, 32)[0]
    size, count, names_index = struct.unpack_from("<HHH", data, 46)
    headers = [struct.unpack_from("<10I", data, offset + i * size) for i in range(count)]
    names_header = headers[names_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    return {names[h[0]:names.index(b"\0", h[0])].decode():
            (h, data[h[4]:h[4] + h[5]] if h[1] != 8 else b"") for h in headers}


class PeerFreePatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = subprocess.check_output([
            "ar", "p", str(ROOT / "vendor/lib_wifi_whc_np.a"), "rtw_nan_peer.o"])

    def test_only_two_cleanup_relocations_change(self):
        old = sections(self.original)
        new = sections(MODULE.patch(self.original))
        self.assertEqual(old.keys(), new.keys())
        changed = {name for name in old if old[name][1] != new[name][1]}
        self.assertEqual(changed, {".symtab", ".strtab", MODULE.SECTION})
        before = old[MODULE.SECTION][1]
        after = new[MODULE.SECTION][1]
        self.assertEqual(len(before), len(after))
        changes = [(struct.unpack_from("<II", before, i),
                    struct.unpack_from("<II", after, i))
                   for i in range(0, len(before), 8) if before[i:i + 8] != after[i:i + 8]]
        self.assertEqual(len(changes), 2)
        self.assertEqual({r[0][0] for r in changes}, {0x48, 0x7a})
        expected = {0x48: (10, b"green2_nan_peer_ndp_free"),
                    0x7a: (30, b"green2_nan_peer_storage_free")}
        for old_rel, new_rel in changes:
            kind, target = expected[old_rel[0]]
            self.assertEqual((old_rel[0], old_rel[1] & 255), (new_rel[0], kind))
            self.assertEqual(new_rel[1] & 255, kind)
            sym = struct.unpack_from("<IIIBBH", new[".symtab"][1], (new_rel[1] >> 8) * 16)
            self.assertEqual(sym[1:], (0, 0, 0x12, 0, 0))
            self.assertEqual(new[".strtab"][1][sym[0]:].split(b"\0")[0], target)

    def test_unknown_vendor_input_is_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.patch(self.original[:-1] + bytes([self.original[-1] ^ 1]))
        with self.assertRaises(ValueError):
            MODULE.patch(b"")


if __name__ == "__main__":
    unittest.main()
