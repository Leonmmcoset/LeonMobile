"""Recovery image admission and target-only path resolution; no host block writes."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('LEON_TEST_BIN', REPO / 'build/host'))
spec = importlib.util.spec_from_file_location('leon_make_initramfs', REPO / 'scripts/make-initramfs.py')
assembler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembler)


class Recovery(unittest.TestCase):
    def test_image_checks_hash_size_ext4_and_unchanged_source(self):
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / 'system.ext4'
            payload = bytearray(65536)
            payload[1080:1082] = b'\x53\xef'
            image.write_bytes(payload)
            digest = hashlib.sha256(payload).hexdigest()

            def verify(hash_value=digest, size=len(payload)):
                return subprocess.run([BUILD / 'bin/leon-recovery', '--verify-image', image,
                                       '--sha256', hash_value, '--size', str(size)],
                                      capture_output=True, text=True, timeout=10).returncode

            self.assertEqual(verify(), 0)
            self.assertNotEqual(verify('0' * 64), 0)
            self.assertNotEqual(verify(size=len(payload) - 1), 0)
            self.assertNotEqual(verify('../' + digest), 0)
            self.assertEqual(image.read_bytes(), payload)
            payload[1080:1082] = b'\0\0'
            image.write_bytes(payload)
            self.assertNotEqual(verify(hashlib.sha256(payload).hexdigest()), 0)

    def test_target_absolute_library_links_never_resolve_on_host(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'usr/lib').mkdir(parents=True)
            (root / 'lib').symlink_to('/usr/lib')
            (root / 'usr/lib/libc.so').write_bytes(b'target library')
            (root / 'usr/lib/loader').symlink_to('libc.so')
            self.assertEqual(assembler.target_file(root, '/lib/loader').read_bytes(), b'target library')
            (root / 'usr/lib/bad').symlink_to('../../../../etc/passwd')
            with self.assertRaises(ValueError):
                assembler.target_file(root, '/lib/bad')
            (root / 'usr/lib/cycle').symlink_to('/lib/cycle')
            with self.assertRaises(ValueError):
                assembler.target_file(root, '/lib/cycle')

    def test_recovery_privileged_entry_refuses_non_pid1(self):
        result = subprocess.run([BUILD / 'bin/leon-recovery'], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('PID 1', result.stderr)


if __name__ == '__main__':
    unittest.main()
