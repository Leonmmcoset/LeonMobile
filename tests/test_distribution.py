"""Exercise installed runtime ownership and the exported, relocated SDK."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


class DistributionTests(unittest.TestCase):
    def test_runtime_payload_and_external_sdk(self):
        # Core-only development is supported; the full CI build exercises this.
        if not (BUILD / "bin/leon-settings").exists():
            self.skipTest("requires graphical application targets")
        with tempfile.TemporaryDirectory(prefix="leon-install-") as temp:
            stage = Path(temp)
            environment = dict(os.environ, DESTDIR=str(stage))

            def install(component):
                subprocess.run(["cmake", "--install", BUILD, "--prefix", "/usr",
                                "--component", component], env=environment,
                               check=True, capture_output=True, text=True)

            install("Runtime")
            self.assertFalse((stage / "usr/include").exists())
            self.assertFalse(list(stage.rglob("*.pc")))
            self.assertFalse(list(stage.rglob("*Config.cmake")))
            manifests = list((stage / "usr/share/leon/apps").glob("*.json"))
            self.assertEqual(len(manifests), 9)
            entries = set()
            for path in manifests:
                manifest = json.loads(path.read_text())
                entry = stage / manifest["entry"].lstrip("/")
                self.assertEqual(entry.read_bytes()[:4], b"\x7fELF")
                self.assertTrue(os.access(entry, os.X_OK))
                self.assertFalse(manifest.get("args"))
                entries.add(entry)
            self.assertEqual(len(entries), 9)
            self.assertFalse((stage / "usr/bin/leon-app").exists())
            notificationd = stage / "usr/sbin/notificationd"
            self.assertEqual(notificationd.read_bytes()[:4], b"\x7fELF")
            self.assertTrue(os.access(notificationd, os.X_OK))

            # Build from the installed SDK, without any source-tree headers.
            install("Development")
            self.assertTrue((stage / "usr/include/lmui/application.hpp").exists())
            self.assertFalse((stage / "usr/include/leon/package.hpp").exists())
            external = stage / "external-build"
            subprocess.run(["cmake", "-S", REPO / "examples/hello", "-B", external,
                            "-G", "Ninja", f"-DCMAKE_PREFIX_PATH={stage / 'usr'}"],
                           check=True, capture_output=True, text=True)
            subprocess.run(["cmake", "--build", external], check=True,
                           capture_output=True, text=True)
            self.assertEqual((external / "hello").read_bytes()[:4], b"\x7fELF")
            platform = stage / "platform-build"
            subprocess.run(["cmake", "-S", REPO / "examples/platform", "-B", platform,
                            "-G", "Ninja", f"-DCMAKE_PREFIX_PATH={stage / 'usr'}"],
                           check=True, capture_output=True, text=True)
            subprocess.run(["cmake", "--build", platform], check=True, capture_output=True, text=True)
            self.assertEqual((platform / "platform-example").read_bytes()[:4], b"\x7fELF")


if __name__ == "__main__":
    unittest.main()
