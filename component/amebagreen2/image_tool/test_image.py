import struct
import unittest
import tempfile
from pathlib import Path
from types import SimpleNamespace
from build_image import merge, validate_core, validate_ota


class ImageRegression(unittest.TestCase):
    def test_missing_device_keys_are_not_regenerated(self):
        with tempfile.TemporaryDirectory() as directory:
            key_file = Path(directory) / 'device.json'
            with self.assertRaisesRegex(FileNotFoundError, 'refusing to generate replacement'):
                merge(SimpleNamespace(sboot='ON', rsip='ON', keys=key_file))
            self.assertFalse(key_file.exists())

    def core(self, sram=0x20068020):
        data = b''
        for address in (0x02000020, sram, 0x60325020):
            data += b'81958711' + struct.pack('<II', 32, address) + b'\xff' * 16 + b'\0' * 32
        return data

    def test_three_contiguous_segments(self):
        validate_core(self.core(), 'km4ns')
        with self.assertRaisesRegex(ValueError, 'unexpected data'):
            validate_core(self.core().ljust(4096, b'\xff'), 'km4ns')

    def test_invalid_entry_table_address(self):
        with self.assertRaisesRegex(ValueError, 'entry-table'):
            validate_core(self.core(0).ljust(4096, b'\xff'), 'km4ns')

    def test_truncated_segment(self):
        with self.assertRaises(ValueError):
            validate_core(self.core()[:45], 'km4ns')

    def test_ota_corruption(self):
        payload = b'81958711' + b'\0' * 24
        header = struct.pack('<8I', 0xffffffff, 1, 0x3141544f, 24, sum(payload), len(payload), 32, 1)
        validate_ota(header + payload)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            validate_ota(header + payload[:-1] + b'\x01')


if __name__ == '__main__':
    unittest.main()
