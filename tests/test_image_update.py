import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import uuid
import zlib

REPO = Path(__file__).resolve().parents[1]
SCRIPT = REPO / "scripts/update-image.py"


class ImageUpdateTests(unittest.TestCase):
    def make_disk(self, path):
        image = bytearray(256 * 512)
        entries = bytearray(128 * 128)
        linux = uuid.UUID("0fc63daf-8483-4772-8e79-3d69d8477de4").bytes_le
        for i, (name, first, last) in enumerate((("system", 34, 65), ("data", 66, 100))):
            off = i * 128
            entries[off:off+16] = linux
            entries[off+16:off+32] = uuid.UUID(int=i+1).bytes_le
            struct.pack_into("<QQ", entries, off+32, first, last)
            encoded = name.encode("utf-16-le")
            entries[off+56:off+56+len(encoded)] = encoded
        header = bytearray(512)
        header[:8] = b"EFI PART"
        struct.pack_into("<II", header, 8, 0x10000, 92)
        struct.pack_into("<QIII", header, 72, 2, 128, 128, zlib.crc32(entries))
        struct.pack_into("<I", header, 16, zlib.crc32(header[:92]))
        image[512:1024] = header
        image[1024:1024+len(entries)] = entries
        image[66*512:101*512] = b"D" * (35*512)
        path.write_bytes(image)
        return bytes(image)

    def test_replace_preserves_partition_table_and_user_data(self):
        with tempfile.TemporaryDirectory() as temp:
            disk = Path(temp) / "disk.img"
            before = self.make_disk(disk)
            payload = bytearray(b"S" * 8192)
            payload[1080:1082] = b"\x53\xef"
            system = Path(temp) / "rootfs.ext4"
            system.write_bytes(payload)
            result = subprocess.run(["python3", SCRIPT, disk, system], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            after = disk.read_bytes()
            self.assertEqual(after[:34*512], before[:34*512])
            self.assertEqual(after[34*512:34*512+len(payload)], payload)
            self.assertEqual(after[66*512:], before[66*512:])

    def test_corrupt_gpt_leaves_disk_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            disk = Path(temp) / "disk.img"
            self.make_disk(disk)
            image = bytearray(disk.read_bytes())
            image[600] ^= 1
            disk.write_bytes(image)
            system = Path(temp) / "rootfs.ext4"
            system.write_bytes(b"x" * 8192)
            result = subprocess.run(["python3", SCRIPT, disk, system], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(disk.read_bytes(), image)


if __name__ == "__main__":
    unittest.main()
