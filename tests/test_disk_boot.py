"""Offline image admission, preservation and boot mode boundaries."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

import test_image_update

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'scripts'))
from leon_image import grub_environment, partitions
from qemu_boot import boot_arguments


class DiskBoot(unittest.TestCase):
    def make_disk(self, path, boot=True, ext4=False):
        test_image_update.ImageUpdateTests().make_disk(path, boot)
        if ext4:
            with path.open('r+b') as stream:
                stream.seek(66 * 512 + 1080)
                stream.write(b'\x53\xef')
        return path.read_bytes()

    def rewrite_entries(self, disk, modify):
        image = bytearray(disk.read_bytes())
        entries = bytearray(image[1024:1024 + 128 * 128])
        modify(entries)
        image[1024:1024 + len(entries)] = entries
        struct.pack_into('<I', image, 512 + 88, zlib.crc32(entries))
        struct.pack_into('<I', image, 512 + 16, 0)
        struct.pack_into('<I', image, 512 + 16, zlib.crc32(image[512:512 + 92]))
        disk.write_bytes(image)

    def test_update_preserves_efi_and_data(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            disk = temp / 'disk.img'
            before = self.make_disk(disk)
            system = temp / 'system.ext4'
            payload = bytearray(b'S' * 8192)
            payload[1080:1082] = b'\x53\xef'
            system.write_bytes(payload)
            result = subprocess.run([sys.executable, REPO / 'scripts/update-image.py', disk, system],
                                    capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            after = disk.read_bytes()
            self.assertEqual(after[66 * 512:], before[66 * 512:])
            self.assertEqual(after[:34 * 512], before[:34 * 512])

    def test_gpt_rejects_overlap_wrong_types_and_reordered_targets(self):
        edits = (
            lambda table: struct.pack_into('<Q', table, 128 + 32, 50),
            lambda table: table.__setitem__(slice(256, 272), table[:16]),
            lambda table: table.__setitem__(slice(56, 128), b'\0' * 72),
        )
        with tempfile.TemporaryDirectory() as name:
            disk = Path(name) / 'disk.img'
            for edit in edits:
                self.make_disk(disk)
                self.rewrite_entries(disk, edit)
                with disk.open('rb') as stream, self.assertRaises(ValueError):
                    partitions(stream)

    def test_migration_retains_complete_data_and_original_disk(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            old = temp / 'old.img'
            before = self.make_disk(old, boot=False, ext4=True)
            baseline = temp / 'leonmobile-qemu-aarch64.img'
            fresh = self.make_disk(baseline, ext4=True)
            new = temp / 'new.img'
            result = subprocess.run([sys.executable, REPO / 'scripts/migrate-boot.py', old, new,
                                     '--images', temp], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(old.read_bytes(), before)
            after = new.read_bytes()
            self.assertEqual(after[66 * 512:101 * 512], before[66 * 512:101 * 512])
            self.assertEqual(after[:66 * 512], fresh[:66 * 512])
            self.assertEqual(after[101 * 512:], fresh[101 * 512:])
            # An existing destination must remain intact.
            result = subprocess.run([sys.executable, REPO / 'scripts/migrate-boot.py', old, new,
                                     '--images', temp], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(new.read_bytes(), after)

    def test_non_ext4_migration_never_publishes_a_destination(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            old, new = temp / 'old.img', temp / 'new.img'
            before = self.make_disk(old, boot=False)
            self.make_disk(temp / 'leonmobile-qemu-aarch64.img')
            result = subprocess.run([sys.executable, REPO / 'scripts/migrate-boot.py', old, new,
                                     '--images', temp], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(new.exists())
            self.assertEqual(old.read_bytes(), before)

    def test_invalid_efi_repair_leaves_entire_disk_unchanged(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            disk = temp / 'disk.img'
            before = self.make_disk(disk)
            (temp / 'leon-efi.vfat').write_bytes(b'\0' * (50 * 512))
            result = subprocess.run([sys.executable, REPO / 'scripts/repair-boot.py', disk,
                                     '--images', temp], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(disk.read_bytes(), before)

    def test_uefi_refuses_legacy_disk_without_creating_variable_bank(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            disk = temp / 'old.img'
            before = self.make_disk(disk, boot=False)
            with self.assertRaisesRegex(ValueError, 'Legacy disk'):
                boot_arguments(temp, disk, temp / 'vars.fd')
            self.assertEqual(disk.read_bytes(), before)
            self.assertFalse((temp / 'vars.fd').exists())

    def test_boot_request_is_bounded_and_rejects_script_injection(self):
        for mode in ('', 'normal', 'safe', 'recovery'):
            block = grub_environment(mode)
            self.assertEqual(len(block), 1024)
            self.assertIn(('next_entry=' + mode + '\n').encode(), block)
        for bad in ('recovery\nroot=evil', 'normal;halt', '../safe'):
            with self.assertRaises(ValueError):
                grub_environment(bad)

    def test_default_uefi_arguments_never_load_host_kernel(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            self.make_disk(temp / 'disk.img')
            for image in ('leon-uefi-code.fd', 'leon-uefi-vars.fd'):
                with (temp / image).open('wb') as stream:
                    stream.truncate(64 * 1024 * 1024)
            args = boot_arguments(temp, temp / 'disk.img', temp / 'runtime.fd')
            self.assertNotIn('-kernel', args)
            self.assertNotIn('-initrd', args)
            self.assertNotIn('-append', args)
            self.assertTrue(any('if=pflash' in arg and 'readonly=on' in arg for arg in args))

    def test_external_recovery_does_not_require_a_readable_system_gpt(self):
        with tempfile.TemporaryDirectory() as name:
            temp = Path(name)
            disk = temp / 'damaged.img'
            disk.write_bytes(b'broken partition table')
            for image in ('leon-uefi-code.fd', 'leon-uefi-vars.fd'):
                with (temp / image).open('wb') as stream:
                    stream.truncate(64 * 1024 * 1024)
            args = boot_arguments(temp, disk, temp / 'runtime.fd', recovery_media=True)
            self.assertTrue(any('usb-storage,drive=rescue' in arg for arg in args))
            self.assertNotIn('-kernel', args)
            self.assertEqual(disk.read_bytes(), b'broken partition table')


if __name__ == '__main__':
    unittest.main()
