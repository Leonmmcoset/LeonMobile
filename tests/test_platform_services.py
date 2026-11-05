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
        for name, handlers in {
            "editor": [{"action": "open", "mime": "text/plain"}, {"action": "share", "mime": "text/plain"}],
            "gallery": [{"action": "open", "scheme": "https"}],
        }.items():
            path = self.builtin / (name + ".json")
            app = json.loads(path.read_text())
            app["handlers"] = handlers
            path.write_text(json.dumps(app))

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

    @contextlib.contextmanager
    def broker(self):
        endpoint = self.root / "apps.sock"
        process = subprocess.Popen([BUILD / "bin/leon-launchd", "--root", self.root,
            "--builtin", self.builtin, "--socket", endpoint, "--runtime-dir", self.root / "users"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 4
            while not endpoint.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertIsNone(process.poll())
            yield endpoint
        finally:
            process.terminate()
            _, error = process.communicate(timeout=6)
            self.assertEqual(process.returncode, 0, error.decode())

    def wait_result(self, endpoint, token, state="delivered", uid=11002):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            result = rpc(endpoint, {"op": "result", "token": token}, uid)
            if result.get("state") == state:
                return result
            time.sleep(.02)
        self.fail("interaction did not settle: " + repr(result))

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

    def test_documents_selection_snapshot_and_revocation(self):
        shared = self.root / "shared"
        shared.mkdir()
        (shared / "note").write_bytes(b"hello\x00world\xff")
        (shared / "escape").symlink_to("/etc/passwd")
        os.mkfifo(shared / "pipe")
        with self.daemon("documentd") as (s, _):
            self.assertFalse(rpc(s, {"op": "choose"}, 12345)["ok"])
            grant = rpc(s, {"op": "choose", "seconds": 120}, 11004)
            self.assertTrue(grant["ok"], grant)
            token = grant["token"]
            self.assertEqual(len(token), 32)
            self.assertFalse(rpc(s, {"op": "pending"}, 11004)["ok"])
            self.assertFalse(rpc(s, {"op": "selection", "token": token}, 11002)["ok"])
            self.assertEqual(rpc(s, {"op": "pending"}, 1000)["requests"][0]["app"], "org.leon.editor")
            for file in ("../builtin/editor.json", "/etc/passwd", "escape", "pipe"):
                self.assertFalse(rpc(s, {"op": "pick", "token": token, "file": file}, 1000)["ok"])
            self.assertFalse(rpc(s, {"op": "pick", "token": token, "file": "note"}, 11004)["ok"])
            self.assertTrue(rpc(s, {"op": "pick", "token": token, "file": "note"}, 1000)["ok"])
            (shared / "note").write_bytes(b"changed")
            data = rpc(s, {"op": "read", "token": token, "offset": 0, "length": 2048}, 11004)
            self.assertEqual(bytes.fromhex(data["hex"]), b"hello\x00world\xff")
            self.assertTrue(data["eof"])
            self.assertFalse(rpc(s, {"op": "read", "token": token, "offset": True, "length": 2}, 11004)["ok"])
            self.assertFalse(rpc(s, {"op": "read", "token": token, "offset": 0, "length": 2049}, 11004)["ok"])
            child = rpc(s, {"op": "delegate", "token": token, "from_uid": 11004, "to_uid": 11002})
            self.assertTrue(child["ok"], child)
            self.assertFalse(rpc(s, {"op": "read", "token": child["token"], "offset": 0, "length": 4}, 11004)["ok"])
            self.assertTrue(rpc(s, {"op": "read", "token": child["token"], "offset": 0, "length": 4}, 11002)["ok"])
            self.assertTrue(rpc(s, {"op": "revoke", "token": token}, 11004)["ok"])
            self.assertFalse(rpc(s, {"op": "read", "token": child["token"], "offset": 0, "length": 4}, 11002)["ok"])

    def test_documents_private_offer_expiry_quota_and_restart(self):
        home = self.root / "data/org.leon.editor"
        home.mkdir(parents=True)
        (home / "draft").write_text("private draft")
        with self.daemon("documentd") as (s, _):
            result = rpc(s, {"op": "offer", "file": "draft", "seconds": 1}, 11004)
            self.assertTrue(result["ok"], result)
            self.assertFalse(rpc(s, {"op": "offer", "file": "../../builtin/editor.json"}, 11004)["ok"])
            self.assertFalse(rpc(s, {"op": "delegate", "token": result["token"],
                                    "from_uid": 11004, "to_uid": 11002}, 11004)["ok"])
            time.sleep(1.1)
            self.assertFalse(rpc(s, {"op": "selection", "token": result["token"]}, 11004)["ok"])
            tokens = [rpc(s, {"op": "choose"}, 11004)["token"] for _ in range(4)]
            self.assertFalse(rpc(s, {"op": "choose"}, 11004)["ok"])
            self.assertTrue(rpc(s, {"op": "cancel", "token": tokens[0]}, 11004)["ok"])
            self.assertEqual(rpc(s, {"op": "selection", "token": tokens[0]}, 11004)["state"], "canceled")
            self.assertTrue(rpc(s, {"op": "reset_app", "app": "org.leon.editor"})["ok"])
            self.assertEqual(rpc(s, {"op": "list"}, 11004)["documents"], [])
        with self.daemon("documentd") as (s, _):
            self.assertFalse(rpc(s, {"op": "selection", "token": tokens[1]}, 11004)["ok"])

    def test_interactions_resolve_choose_delivery_and_durable_defaults(self):
        with self.broker(), self.daemon("intentd") as (s, _):
            payload = {"action": "share", "mime": "text/plain", "text": "hello 中文"}
            resolved = rpc(s, {"op": "resolve", **payload}, 11002)
            self.assertEqual([a["id"] for a in resolved["candidates"]], ["org.leon.editor"])
            token = rpc(s, {"op": "request", **payload}, 11002)["token"]
            self.assertFalse(rpc(s, {"op": "pending"}, 11002)["ok"])
            self.assertFalse(rpc(s, {"op": "choose", "token": token, "target": "org.leon.editor"}, 11002)["ok"])
            self.assertFalse(rpc(s, {"op": "choose", "token": token, "target": "org.leon.files"}, 1000)["ok"])
            self.assertTrue(rpc(s, {"op": "choose", "token": token, "target": "org.leon.editor", "always": True}, 1000)["ok"])
            self.wait_result(s, token)
            self.assertEqual(rpc(s, {"op": "receive"}, 11003)["events"], [])
            events = rpc(s, {"op": "receive"}, 11004)["events"]
            self.assertEqual(events[0]["payload"]["text"], payload["text"])
            self.assertEqual(events[0]["source"], "org.leon.gallery")
            self.assertFalse(rpc(s, {"op": "ack", "token": token}, 11002)["ok"])
            self.assertTrue(rpc(s, {"op": "ack", "token": token}, 11004)["ok"])
            self.assertEqual(rpc(s, {"op": "receive"}, 11004)["events"], [])
            self.assertTrue(rpc(s, {"op": "result", "token": token}, 11002)["acknowledged"])
        with self.daemon("intentd") as (s, _):
            self.assertIn("share:mime:text/plain", rpc(s, {"op": "defaults"}, 11000)["defaults"])
            token = rpc(s, {"op": "request", **payload}, 11002)["token"]
            self.assertEqual(rpc(s, {"op": "pending"}, 1000)["requests"][0]["default"], "org.leon.editor")
            self.assertTrue(rpc(s, {"op": "clear_default", "key": "share:mime:text/plain"}, 11000)["ok"])
            self.assertNotIn("default", rpc(s, {"op": "pending"}, 1000)["requests"][0])
            self.assertTrue(rpc(s, {"op": "cancel", "token": token}, 11002)["ok"])

    def test_interactions_reject_unsafe_payload_and_foreign_documents(self):
        with self.broker(), self.daemon("documentd") as (docs, _), self.daemon("intentd") as (s, _):
            for payload in ({"action": "exec", "uri": "https://example.com"},
                {"action": "open", "uri": "file:///etc/passwd"},
                {"action": "open", "uri": "javascript:alert(1)"},
                {"action": "open", "uri": "https://"},
                {"action": "open", "uri": "https://test\n--arg"},
                {"action": "share", "mime": "text/plain", "text": 7},
                {"action": "share", "mime": "text/plain", "text": "x", "document": "0" * 32}):
                self.assertFalse(rpc(s, {"op": "request", **payload}, 11002)["ok"])
            self.assertFalse(rpc(s, {"op": "request", "action": "share", "mime": "text/plain", "text": "x"}, 12345)["ok"])
            home = self.root / "data/org.leon.editor"
            home.mkdir(parents=True, exist_ok=True)
            (home / "secret").write_text("private")
            document = rpc(docs, {"op": "offer", "file": "secret"}, 11004)["token"]
            token = rpc(s, {"op": "request", "action": "open", "mime": "text/plain", "document": document}, 11002)["token"]
            self.assertTrue(rpc(s, {"op": "choose", "token": token, "target": "org.leon.editor"}, 1000)["ok"])
            self.wait_result(s, token, "failed")
            self.assertEqual(rpc(s, {"op": "receive"}, 11004)["events"], [])
            token = rpc(s, {"op": "request", "action": "open", "mime": "text/plain", "document": document}, 11004)["token"]
            self.assertTrue(rpc(s, {"op": "choose", "token": token, "target": "org.leon.editor"}, 1000)["ok"])
            self.wait_result(s, token, uid=11004)
            received = rpc(s, {"op": "receive"}, 11004)["events"][0]["payload"]["document"]
            self.assertNotEqual(received, document)
            self.assertEqual(bytes.fromhex(rpc(docs, {"op": "read", "token": received, "offset": 0, "length": 20}, 11004)["hex"]), b"private")

    def test_network_policy_validation_persistence_and_watch(self):
        with self.daemon("netd") as (s, _):
            initial = rpc(s, {"op": "status"})
            self.assertEqual(initial["configuration"]["method"], "dhcp")
            self.assertFalse(rpc(s, {"op": "configure", "values": {"enabled": False}}, 11004)["ok"])
            for values in ({"method": "static"}, {"prefix": True}, {"dns": "1.1.1.1"},
                           {"dns": ["bad"]}, {"dns": ["0.0.0.0"]}, {"interface": "lo"},
                           {"method": "static", "address": "10.0.2.15", "gateway": "10.2.3.4"}):
                self.assertFalse(rpc(s, {"op": "configure", "values": values}, 11000)["ok"])
            result = rpc(s, {"op": "configure", "values": {"method": "static", "address": "10.0.2.15",
                "gateway": "10.0.2.2", "prefix": 24, "dns": ["10.0.2.3", "::1"]}}, 11000)
            self.assertTrue(result["ok"], result)
            self.assertTrue(rpc(s, {"op": "watch", "since": initial["generation"]})["changed"])
            current = rpc(s, {"op": "status"})["generation"]
            self.assertFalse(rpc(s, {"op": "watch", "since": current})["changed"])
        with self.daemon("netd") as (s, _):
            self.assertEqual(rpc(s, {"op": "status"})["configuration"]["method"], "static")

    def test_audio_pcm_ownership_volume_focus_pause_and_restart(self):
        with self.daemon("settingsd"), self.daemon("audiod") as (s, _):
            self.assertFalse(rpc(s, {"op": "open"}, 12345)["ok"])
            self.assertFalse(rpc(s, {"op": "open", "rate": True}, 11002)["ok"])
            self.assertFalse(rpc(s, {"op": "set_master", "volume": 50}, 11002)["ok"])
            first = rpc(s, {"op": "open", "title": "Track"}, 11002)["token"]
            self.assertFalse(rpc(s, {"op": "write", "token": first, "hex": "1027" * 1024}, 11004)["ok"])
            self.assertFalse(rpc(s, {"op": "write", "token": first, "hex": "xx"}, 11002)["ok"])
            self.assertTrue(rpc(s, {"op": "set_volume", "volume": 50}, 11002)["ok"])
            written = rpc(s, {"op": "write", "token": first, "hex": "1027" * 1024}, 11002)
            self.assertEqual(written["accepted"], 2048)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                state = rpc(s, {"op": "stream", "token": first}, 11002)["stream"]
                if state["played_bytes"] >= 2048:
                    break
            self.assertEqual(state["played_bytes"], 2048)
            self.assertEqual(state["peak"], 3500)
            second = rpc(s, {"op": "open", "title": "Other"}, 11004)["token"]
            self.assertTrue(rpc(s, {"op": "stream", "token": first}, 11002)["stream"]["focus_lost"])
            self.assertEqual(rpc(s, {"op": "write", "token": first, "hex": "1027" * 8}, 11002)["accepted"], 0)
            self.assertTrue(rpc(s, {"op": "pause", "token": second}, 1000)["ok"])
            self.assertFalse(rpc(s, {"op": "stream", "token": first}, 11002)["stream"]["focus_lost"])
            self.assertEqual(len(rpc(s, {"op": "status"}, 11002)["streams"]), 1)
            self.assertNotIn("current", rpc(s, {"op": "status"}, 11002))
            self.assertEqual(rpc(s, {"op": "status"}, 1000)["current"]["title"], "Other")
            self.assertTrue(rpc(s, {"op": "set_master", "volume": 25}, 11000)["ok"])
            self.assertTrue(rpc(s, {"op": "set_master", "muted": True}, 11000)["ok"])
            self.assertTrue(rpc(s, {"op": "close", "token": first}, 11002)["ok"])
        with self.daemon("audiod") as (s, _):
            self.assertTrue(rpc(s, {"op": "status"})["muted"])
            self.assertEqual(rpc(s, {"op": "status"})["streams"], [])
            token = rpc(s, {"op": "open"}, 11002)["token"]
            self.assertEqual(rpc(s, {"op": "stream", "token": token}, 11002)["stream"]["volume"], 50)
