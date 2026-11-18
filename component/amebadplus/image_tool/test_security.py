import struct
import unittest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
import hashlib
from dplus_security import verify_signed


class SignatureTest(unittest.TestCase):
    def setUp(self):
        # Ephemeral unit-test keys: never used by the firmware packer.
        root, app = Ed25519PrivateKey.generate(), Ed25519PrivateKey.generate()
        def public(key):
            return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        rp, ap = public(root), public(app)
        def manifest(payload, key):
            data = bytearray(4096)
            data[:8] = b'81958711'
            data[16] = 1
            struct.pack_into('<I', data, 24, len(payload))
            data[48:80] = hashlib.sha256(payload).digest()
            data[112:144] = public(key)
            data[256:320] = key.sign(bytes(data[:256]))
            return data
        self.boot_body, self.app_body = b'boot fixture', b'KM0 KM4 SPE fixture'
        self.boot = manifest(self.boot_body, root) + self.boot_body
        cert = bytearray(4096)
        cert[16] = 1
        struct.pack_into('<II', cert, 24, 212, 1)
        cert[32:64] = rp
        cert[180:212] = hashlib.sha256(ap).digest()
        cert[212:276] = root.sign(bytes(cert[:212]))
        self.app = cert + manifest(self.app_body, app) + self.app_body
        self.config = {'cert': {'sboot_public_key': rp.hex()}, 'image2': {'sboot_public_key': ap.hex()}}

    def verify(self):
        verify_signed(bytes(self.boot), bytes(self.app), self.boot_body, self.app_body, self.config)

    def test_valid_chain(self):
        self.verify()

    def test_modified_certificate_and_manifest(self):
        for location in (32, 180, 212, 4096+48, 4096+112, 4096+256):
            with self.subTest(location=location):
                self.app[location] ^= 1
                with self.assertRaises((ValueError, InvalidSignature)):
                    self.verify()
                self.app[location] ^= 1

    def test_modified_boot_and_secure_payload(self):
        self.boot[256] ^= 1
        with self.assertRaises(InvalidSignature):
            self.verify()
        self.boot[256] ^= 1
        self.app_body = b'KM0 KM4 BAD fixture'
        with self.assertRaises(ValueError):
            self.verify()


if __name__ == '__main__':
    unittest.main()
