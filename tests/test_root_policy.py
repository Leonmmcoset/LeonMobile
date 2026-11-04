"""Exercise image finalization against stale account and privilege entry points."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class RootImagePolicy(unittest.TestCase):
    def test_existing_image_locks_root_and_rejects_sudo(self):
        with tempfile.TemporaryDirectory(prefix="leon-root-image-") as directory:
            target = Path(directory)
            for name in ("etc/leon/services", "usr/libexec/leon", "usr/bin", "bin", "sbin"):
                (target / name).mkdir(parents=True, exist_ok=True)
            (target / "etc/leon/services/prepare.toml").touch()
            (target / "usr/libexec/leon/prepare").touch()
            (target / "etc/shadow").write_text("root:old-password:1:2:3:4:5:6:7\nleon:!:8:9:10:11:12:13:14\n")
            (target / "bin/su").symlink_to("busybox")
            (target / "sbin/sulogin").symlink_to("../bin/busybox")
            (target / "usr/bin/sudo").write_bytes((REPO / "distro/rootfs-overlay/usr/bin/sudo").read_bytes())
            script = REPO / "distro/board/qemu-arm64/post-build.sh"
            for _ in range(2):
                subprocess.run(["bash", script, target], check=True, capture_output=True)
                self.assertEqual((target / "etc/shadow").read_text(),
                                 "root:!:1:2:3:4:5:6:7\nleon:!:8:9:10:11:12:13:14\n")
                self.assertEqual((target / "etc/shadow").stat().st_mode & 0o777, 0o600)
                self.assertFalse(os.path.lexists(target / "bin/su"))
                self.assertFalse(os.path.lexists(target / "sbin/sulogin"))
                for name in ("sudo", "sudoedit"):
                    result = subprocess.run([target / "usr/bin" / name, "id"], capture_output=True)
                    self.assertEqual(result.returncode, 126)
                    self.assertIn(b"disabled by system policy", result.stderr)
