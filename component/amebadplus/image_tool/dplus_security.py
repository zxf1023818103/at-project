"""Per-workspace keys and independent validation of the SDK's v1 image format.

This module creates build artifacts only. Provisioning eFuses is a separate,
irreversible operation; neither signing nor encryption writes to the device.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def prepare(template, output, key_file, sboot, rsip):
    import json5
    config = json5.loads(Path(template).read_text())
    key_file = Path(key_file)
    if sboot or rsip:
        if not key_file.exists():
            key_file.parent.mkdir(parents=True, exist_ok=True)
            keys = {name: os.urandom(32).hex() for name in ('root', 'app', 'rsip_data', 'rsip_tweak')}
            keys.update(boot_iv=os.urandom(8).hex(), app_iv=os.urandom(8).hex())
            # Exclusive creation protects an existing key set from concurrent builds.
            try:
                fd = os.open(key_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(fd, 'w') as stream:
                    json.dump(keys, stream, indent=2)
            except FileExistsError:
                pass
        keys = json.loads(key_file.read_text())
        for section, key_name in [('image1', 'root'), ('cert', 'root'), ('image2', 'app')]:
            seed = bytes.fromhex(keys[key_name])
            public = Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(
                serialization.Encoding.Raw, serialization.PublicFormat.Raw)
            config[section].update(sboot_private_key=seed.hex(), sboot_public_key=public.hex(),
                                   sboot_public_key_hash=hashlib.sha256(public).hexdigest())
        config['image1']['rsip_iv'] = keys['boot_iv']
        config['image2']['rsip_iv'] = keys['app_iv']
        config['otp_key_0x2C0'] = keys['rsip_data']
        config['otp_key_0x2E0'] = keys['rsip_tweak']
    config.update(sboot_enable=sboot, rsip_enable=rsip)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'manifest.json5').write_text(json.dumps(config, indent=2))
    (output / 'manifest.json5').chmod(0o600)
    (output / 'soc_info.json').write_text('{"soc":{"name":"RTL8711Dx"}}\n')
    shutil.copyfile(Path(template).with_name('ameba_layout.ld'), output / 'ameba_layout.ld')
    return config


def verify_manifest(data, payload, public):
    if len(data) < 4096 or data[16] != 1 or data[18:20] != b'\x00\x00':
        raise ValueError('unsupported signed manifest format')
    if struct.unpack_from('<I', data, 24)[0] != len(payload):
        raise ValueError('manifest payload size mismatch')
    if data[48:80] != hashlib.sha256(payload).digest():
        raise ValueError('manifest payload hash mismatch')
    if data[112:144] != public:
        raise ValueError('manifest public key does not match configured key')
    Ed25519PublicKey.from_public_bytes(public).verify(data[256:320], data[:256])


def verify_signed(boot, app, boot_plain, app_plain, config):
    root = bytes.fromhex(config['cert']['sboot_public_key'])
    app_key = bytes.fromhex(config['image2']['sboot_public_key'])
    verify_manifest(boot[:4096], boot_plain, root)
    table_size, count = struct.unpack_from('<II', app, 24)
    if app[16] != 1 or table_size != 176 + count * 36 or not 1 <= count <= 5:
        raise ValueError('invalid certificate table')
    if app[32:64] != root or struct.unpack_from('<I', app, 176)[0] != 0:
        raise ValueError('invalid certificate root/application key ID')
    if app[180:212] != hashlib.sha256(app_key).digest():
        raise ValueError('certificate application key hash mismatch')
    Ed25519PublicKey.from_public_bytes(root).verify(app[table_size:table_size+64], app[:table_size])
    verify_manifest(app[4096:8192], app_plain, app_key)


def decrypt_rsip(data, address, config, section):
    """Invert Dplus RSIP XTS (AES whitening), independently of the SDK tool."""
    if len(data) % 16:
        raise ValueError('RSIP data must be block aligned')
    data_key = bytes.fromhex(config['otp_key_0x2C0'])
    tweak_key = bytes.fromhex(config['otp_key_0x2E0'])
    iv = bytes.fromhex(config[section]['rsip_iv'])
    whitening = Cipher(algorithms.AES(tweak_key), modes.ECB()).encryptor()
    decrypt = Cipher(algorithms.AES(data_key), modes.ECB()).decryptor()
    result = bytearray()
    for off in range(0, len(data), 16):
        addr = (address + off) & 0x0fffffff
        counter = iv + struct.pack('>II', addr // 32, 2 + (off // 16) % 2)
        mask = whitening.update(counter)
        block = bytes(a ^ b for a, b in zip(data[off:off+16], mask))
        result.extend(a ^ b for a, b in zip(decrypt.update(block), mask))
    return bytes(result)
