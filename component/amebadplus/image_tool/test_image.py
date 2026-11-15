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

    def core(self, entry=0x20004d20):
        data = b''
        for address in (0xc000020, 0x20068020, 0x60000020, entry):
            data += b'81958711' + struct.pack('<II', 32, address) + b'\xff' * 16 + b'\0' * 32
        return data

    def test_km0_requires_padding(self):
        with self.assertRaisesRegex(ValueError, '4 KiB'):
            validate_core(self.core(), 'km0')
        validate_core(self.core().ljust(4096, b'\xff'), 'km0')

    def test_entry_is_table_not_function(self):
        with self.assertRaisesRegex(ValueError, 'entry-table'):
            validate_core(self.core(0x20004d40).ljust(4096, b'\xff'), 'km0')

    def test_truncated_segment(self):
        with self.assertRaises(ValueError):
            validate_core(self.core()[:45], 'km0')

    def test_ota_corruption(self):
        payload = b'81958711' + b'\0' * 24
        header = struct.pack('<8I', 0xffffffff, 1, 0x3141544f, 24, sum(payload), len(payload), 32, 1)
        validate_ota(header + payload)
        with self.assertRaisesRegex(ValueError, 'checksum'):
            validate_ota(header + payload[:-1] + b'\x01')


if __name__ == '__main__':
    unittest.main()
