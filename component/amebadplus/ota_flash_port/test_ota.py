import pathlib
import os
import struct
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent


class OtaWriterTest(unittest.TestCase):
    def test_nor_transactions(self):
        self.exercise(False)

    def test_tfm_nor_transactions(self):
        self.exercise(True)

    def exercise(self, tfm):
        def core(entry):
            result = bytearray()
            for address in (0x0c000020, 0x20010020, 0x60000020, entry):
                result += b'81958711' + struct.pack('<II', 32, address) + b'\xff' * 16
                result += bytes(range(32))
            return result

        km0 = core(0x20004d20)
        km0.extend(b'\xff' * (-len(km0) % 4096))
        bodies = km0 + core(0x20004da0)
        spe_start = 8192 + len(bodies)
        if tfm:
            spe = bytearray(b'81958711' + struct.pack('<II', 1056, 0x0f000020) + b'\xff' * 16)
            spe.extend(b'\xff' * 1056)
            struct.pack_into('<II', spe, 1024, 0x3000c000, 0x0f001001)
            bodies += spe
        cert = b'81958711' + b'\xff' * (4096 - 8)
        manifest = bytearray(cert)
        manifest[16] = 1
        struct.pack_into('<I', manifest, 24, len(bodies))
        app = cert + manifest + bodies
        def pack(payload):
            return struct.pack('<8I', 0xffffffff, 1, 0x3141544f, 24,
                               sum(payload) & 0xffffffff, len(payload), 32, 1) + payload
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            image = tmp / 'ota.bin'
            image.write_bytes(pack(app))
            exe = tmp / 'test_ota'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            *(['-DAT_ENABLE_TFM'] if tfm else []),
                            '-I' + str(HERE), '-I' + str(HERE.parents[1] / 'ota_flash_config'),
                            str(HERE / 'ota_flash_dplus.c'), str(HERE / 'test_ota.c'),
                            '-o', str(exe)], check=True)
            # The managed runner uses ptrace; LeakSanitizer cannot run there.
            # Address/undefined-behavior instrumentation remains enabled.
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            result = subprocess.run([str(exe), str(image)], capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS:', result.stdout)
            invalid = {}
            bad = bytearray(app)
            struct.pack_into('<I', bad, 4096 + 24, len(bodies) + 32)
            invalid['manifest size'] = bad
            bad = bytearray(app)
            struct.pack_into('<I', bad, 8192 + 4096 + 3 * 64 + 12, 0x20004d20)
            invalid['KM4 entry address'] = bad
            bad = bytearray(app) + b'\xff' * 32
            struct.pack_into('<I', bad, 4096 + 24, len(bad) - 8192)
            invalid['trailing bytes'] = bad
            if tfm:
                for name, off, value in [('SPE address', 12, 0x0e000020),
                                         ('NS stack', 1024, 0x20010000),
                                         ('NS entry', 1028, 0x0e001001),
                                         ('non-Thumb entry', 1028, 0x0f001000)]:
                    bad = bytearray(app)
                    struct.pack_into('<I', bad, spe_start + off, value)
                    invalid[name] = bad
                for name, end in [('missing SPE', spe_start), ('truncated SPE', len(app) - 32)]:
                    bad = bytearray(app[:end])
                    struct.pack_into('<I', bad, 4096 + 24, len(bad) - 8192)
                    invalid[name] = bad
            for name, payload in invalid.items():
                with self.subTest(tfm=tfm, invalid=name):
                    image.write_bytes(pack(payload))
                    result = subprocess.run([str(exe), str(image), 'reject'],
                                            capture_output=True, text=True, env=env)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
