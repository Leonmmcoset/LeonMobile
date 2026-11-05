"""Isolated real-credential tests for settings, interactions, documents and audio."""
import contextlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

from test_system_services import rpc

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


@unittest.skipUnless(os.geteuid() == 0, "real service credentials require root")
class PlatformServices(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.root.chmod(0o755)
        self.builtin = self.root / "builtin"
        self.builtin.mkdir()
        for name in ("settings", "editor", "gallery", "files"):
            (self.builtin / (name + ".json")).write_text(json.dumps(dict(
                format=1, id="org.leon." + name, version="1.0.0", arch="aarch64", lmui=1,
                name=name, entry="/usr/bin/true", permissions=["files.import"])))

    def tearDown(self):
        self.temp.cleanup()

    @contextlib.contextmanager
    def daemon(self, name, extra=(), state=None):
        endpoint = self.root / (name + ".sock")
        command = [BUILD / "bin" / name, "--test", "--socket", endpoint,
                   "--root", self.root, "--builtin", self.builtin,
                   "--state", state or self.root / (name + ".json"), *extra]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    rpc(endpoint, {"op": "__probe"})
                    break
                except (OSError, ValueError):
                    time.sleep(.01)
            self.assertIsNone(process.poll(), "service failed to start")
            yield endpoint, process
        finally:
            if process.poll() is None:
                process.terminate()
            _, error = process.communicate(timeout=6)
            self.assertEqual(process.returncode, 0, error.decode())

    def test_settings_schema_atomic_update_and_watch(self):
        with self.daemon("settingsd") as (s, _):
            initial = rpc(s, {"op": "status"})
            self.assertEqual(initial["settings"]["volume"], 70)
            self.assertEqual(rpc(s, {"op": "schema"})["schema"]["volume"]["max"], 100)
            generation = initial["generation"]
            self.assertFalse(rpc(s, {"op": "watch", "since": generation})["changed"])
            self.assertFalse(rpc(s, {"op": "watch", "since": True})["ok"])
            for uid in (11002, 12000, 12345):
                self.assertFalse(rpc(s, {"op": "set", "key": "dark", "value": True}, uid)["ok"])
            for values in ({"dark": "true"}, {"volume": True}, {"volume": 101},
                           {"volume": 3.5}, {"language": "xx"}, {"navigation": "other"},
                           {"unknown": 1}, {"dark": True, "volume": -1}):
                self.assertFalse(rpc(s, {"op": "update", "values": values}, 11000)["ok"])
                self.assertEqual(rpc(s, {"op": "status"})["generation"], generation)
            result = rpc(s, {"op": "update", "values": {"dark": True, "language": "en"},
                             "expected_generation": generation}, 11000)
            self.assertTrue(result["ok"], result)
            self.assertEqual(result["generation"], generation + 1)
            self.assertTrue(rpc(s, {"op": "watch", "since": generation})["changed"])
            self.assertFalse(rpc(s, {"op": "set", "key": "dark", "value": False,
                                    "expected_generation": generation}, 1000)["ok"])
            self.assertEqual(rpc(s, {"op": "set", "key": "dark", "value": True}, 1000)
                             ["generation"], generation + 1)
        with self.daemon("settingsd") as (s, _):
            self.assertEqual(rpc(s, {"op": "status"})["generation"], generation + 1)
            self.assertTrue(rpc(s, {"op": "status"})["settings"]["dark"])

    def test_settings_legacy_migration_and_storage_failure(self):
        (self.root / "settings.json").write_text(json.dumps({"dark": True, "volume": 25}))
        state = self.root / "settingsd.json"
        with self.daemon("settingsd") as (s, _):
            initial = rpc(s, {"op": "status"})
            self.assertTrue(initial["settings"]["dark"])
            self.assertEqual(initial["settings"]["volume"], 25)
            self.assertFalse(initial["settings"]["reduce_motion"])
            state.unlink()
            state.mkdir()
            self.assertFalse(rpc(s, {"op": "set", "key": "dark", "value": False})["ok"])
            self.assertEqual(rpc(s, {"op": "status"}), initial)
        state.rmdir()
        state.write_text('{"format":1,"generation":1,"settings":{"dark":"false"}}')
        failed = subprocess.run([BUILD / "bin/settingsd", "--test", "--root", self.root,
                                 "--state", state, "--socket", self.root / "broken.sock"],
                                capture_output=True, timeout=4)
        self.assertNotEqual(failed.returncode, 0)
