"""Exercise the notification daemon through sockets and real Unix identities."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


@unittest.skipUnless(os.geteuid() == 0, "notification peer authorization requires root")
class NotificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leon-notify-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.path.chmod(0o755)
        self.root = self.path / "root"
        self.root.mkdir(mode=0o755)
        self.builtin = self.path / "builtin"
        self.builtin.mkdir(mode=0o755)
        self.manifest = dict(format=1, id="org.leon.gallery", version="1.0.0",
                             arch="aarch64", lmui=1, name="Gallery", entry="/usr/bin/true")
        (self.builtin / "gallery.json").write_text(json.dumps(self.manifest))
        self.endpoint = str(self.path / "notifications.sock")
        self.command = [BUILD / "bin/notificationd", "--root", self.root,
                        "--builtin", self.builtin, "--socket", self.endpoint]
        self.daemon = self.start()

    def start(self):
        proc = subprocess.Popen(self.command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self.stop, proc)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                self.fail("notificationd exited: " + proc.communicate()[1].decode())
            if Path(self.endpoint).exists():
                try:
                    if self.rpc({"op": "notifications"})["ok"]:
                        return proc
                except (OSError, ValueError):
                    pass
            time.sleep(.02)
        self.fail("notificationd did not become ready")

    @staticmethod
    def stop(proc, sig=signal.SIGTERM):
        if proc.poll() is None:
            proc.send_signal(sig)
        proc.communicate(timeout=5)

    def rpc(self, message, uid=0, endpoint=None):
        endpoint = endpoint or self.endpoint
        payload = json.dumps(message).encode() + b"\n"
        if uid:
            # Credentials must be established before connecting, rather than
            # supplied in request fields.
            code = (
                "import json,socket; s=socket.socket(socket.AF_UNIX); s.settimeout(3); "
                f"s.connect({endpoint!r}); s.sendall({payload!r}); s.shutdown(socket.SHUT_WR); "
                "out=bytearray()\n"
                "while block:=s.recv(4096): out.extend(block)\n"
                "print(out.decode())\n"
            )
            proc = subprocess.run([sys.executable, "-c", code], user=uid, group=uid,
                                  extra_groups=[], capture_output=True, text=True, timeout=5)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            return json.loads(proc.stdout)
        return json.loads(self.raw(payload, endpoint))

    def raw(self, payload, endpoint=None):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(3)
            client.connect(endpoint or self.endpoint)
            client.sendall(payload)
            client.shutdown(socket.SHUT_WR)
            output = bytearray()
            while block := client.recv(4096):
                output.extend(block)
            return output

    def test_peer_identity_catalog_and_legacy_relay(self):
        self.assertTrue(self.rpc({"op": "notify", "title": "你好，LeonMobile！",
                                  "app": "org.leon.settings", "uid": 11000}, uid=11002)["ok"])
        self.assertEqual(self.rpc({"op": "notifications"}, uid=1000)["notifications"],
                         [{"app": "org.leon.gallery", "title": "你好，LeonMobile！"}])
        for uid in (0, 1000, 11999):
            self.assertFalse(self.rpc({"op": "notify", "title": "unregistered"}, uid=uid)["ok"])
        self.assertFalse(self.rpc({"op": "notifications"}, uid=11002)["ok"])
        self.assertFalse(self.rpc({"op": "notify_for", "uid": 11002, "title": "spoof"},
                                  uid=11002)["ok"])
        self.assertTrue(self.rpc({"op": "notify_for", "uid": 11002, "title": "legacy"})["ok"])
        for uid in (-1, 2**32, "11002", True):
            self.assertFalse(self.rpc({"op": "notify_for", "uid": uid, "title": "bad"})["ok"])

        # Installed identities use the shared catalog and disappear on removal,
        # even though the persistent UID allocation remains reserved.
        installed = dict(self.manifest, id="org.example.notes", entry="bin/notes")
        manifest = self.root / "apps/org.example.notes/current/manifest.json"
        manifest.parent.mkdir(parents=True)
        manifest.write_text(json.dumps(installed))
        (self.root / "identities.json").write_text(json.dumps(
            {"next": 12001, "apps": {"org.example.notes": 12000}}))
        self.assertTrue(self.rpc({"op": "notify", "title": "installed"}, uid=12000)["ok"])
        self.assertEqual(self.rpc({"op": "notifications"})["notifications"][-1]["app"],
                         "org.example.notes")
        manifest.unlink()
        self.assertFalse(self.rpc({"op": "notify", "title": "removed"}, uid=12000)["ok"])

    def test_bounded_queue_and_protocol_validation(self):
        for index in range(70):
            self.assertTrue(self.rpc({"op": "notify_for", "uid": 11002,
                                      "title": f"message {index:02d}"})["ok"])
        items = self.rpc({"op": "notifications"})["notifications"]
        self.assertEqual(len(items), 64)
        self.assertEqual([item["title"] for item in items],
                         [f"message {index:02d}" for index in range(6, 70)])
        title = "界" * 53 + "!"
        self.assertEqual(len(title.encode()), 160)
        self.assertTrue(self.rpc({"op": "notify", "title": title}, uid=11002)["ok"])
        self.assertFalse(self.rpc({"op": "notify", "title": title + "!"}, uid=11002)["ok"])
        # Escaped content must survive the C storage/JSON boundary, including NUL.
        escaped = 'line\n"\\\x00end'
        self.assertTrue(self.rpc({"op": "notify", "title": escaped}, uid=11002)["ok"])
        self.assertEqual(self.rpc({"op": "notifications"})["notifications"][-1]["title"], escaped)
        for message in (None, [], {}, {"op": "unknown"}, {"op": 1},
                        {"op": "notify_for", "uid": 11002},
                        {"op": "notify_for", "uid": 11002, "title": 42}):
            self.assertFalse(self.rpc(message)["ok"])
        for payload in (b"not json\n", b'{"op":"notifications"}',
                        b'{"op":"notifications"}\x00\n', b'{"op":"\xff"}\n',
                        b'{"op":"notify","title":"\xff"}\n', b" " * 8192 + b"\n"):
            self.assertFalse(json.loads(self.raw(payload))["ok"])
        self.assertTrue(json.loads(self.raw(b" " * (8191 - 22) +
                                            b'{"op":"notifications"}\n'))["ok"])

    def test_slow_clients_restart_and_socket_ownership(self):
        with socket.socket(socket.AF_UNIX) as idle, socket.socket(socket.AF_UNIX) as fragmented:
            idle.connect(self.endpoint)
            idle.settimeout(3)
            fragmented.connect(self.endpoint)
            fragmented.sendall(b'{"op":')
            self.assertTrue(self.rpc({"op": "notifications"})["ok"])
            fragmented.sendall(b'"notifications"}\n')
            fragmented.settimeout(3)
            self.assertTrue(json.loads(fragmented.recv(4096))["ok"])
            self.assertEqual(idle.recv(1), b"", "idle clients must have an absolute deadline")

        duplicate = subprocess.run(self.command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(duplicate.returncode, 0)
        self.assertTrue(self.rpc({"op": "notify_for", "uid": 11002, "title": "before crash"})["ok"])
        self.stop(self.daemon, signal.SIGKILL)
        self.assertTrue(Path(self.endpoint).exists())
        self.daemon = self.start()
        self.assertEqual(self.rpc({"op": "notifications"})["notifications"], [])
        self.stop(self.daemon)
        self.assertFalse(Path(self.endpoint).exists())
        Path(self.endpoint).write_text("unrelated file")
        refused = subprocess.run(self.command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(refused.returncode, 0)
        self.assertEqual(Path(self.endpoint).read_text(), "unrelated file")

    def test_broker_compatibility_and_independent_lifecycles(self):
        apps_endpoint = str(self.path / "apps.sock")

        def start_broker():
            broker = subprocess.Popen(
                [BUILD / "bin/leon-launchd", "--root", self.root, "--builtin", self.builtin,
                 "--socket", apps_endpoint, "--notifications-socket", self.endpoint],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.addCleanup(self.stop, broker)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if broker.poll() is not None:
                    self.fail("broker exited: " + broker.communicate()[1].decode())
                if Path(apps_endpoint).exists():
                    try:
                        if self.rpc({"op": "list"}, endpoint=apps_endpoint)["ok"]:
                            return broker
                    except (OSError, ValueError):
                        pass
                time.sleep(.02)
            self.fail("broker did not become ready")

        broker = start_broker()
        self.assertTrue(self.rpc({"op": "notify", "title": "legacy", "uid": 11000},
                                 uid=11002, endpoint=apps_endpoint)["ok"])
        self.assertFalse(self.rpc({"op": "notifications"}, uid=11002,
                                  endpoint=apps_endpoint)["ok"])
        self.assertFalse(self.rpc({"op": "notify", "title": "unknown"}, uid=11999,
                                  endpoint=apps_endpoint)["ok"])
        self.stop(broker)
        self.assertTrue(self.rpc({"op": "notify", "title": "without broker"}, uid=11002)["ok"])
        broker = start_broker()
        direct = self.rpc({"op": "notifications"})["notifications"]
        self.assertEqual(direct, [{"app": "org.leon.gallery", "title": "legacy"},
                                  {"app": "org.leon.gallery", "title": "without broker"}])
        self.assertEqual(self.rpc({"op": "notifications"}, uid=1000,
                                  endpoint=apps_endpoint)["notifications"], direct)

        self.stop(self.daemon)
        self.assertFalse(self.rpc({"op": "notify", "title": "unavailable"}, uid=11002,
                                  endpoint=apps_endpoint)["ok"])
        self.assertFalse(self.rpc({"op": "notifications"}, endpoint=apps_endpoint)["ok"])
        self.assertTrue(self.rpc({"op": "settings"}, endpoint=apps_endpoint)["ok"])
        self.assertTrue(self.rpc({"op": "list"}, endpoint=apps_endpoint)["ok"])
        self.daemon = self.start()
        self.assertEqual(self.rpc({"op": "notifications"},
                                  endpoint=apps_endpoint)["notifications"], [])
        self.assertTrue(self.rpc({"op": "notify", "title": "after restart"}, uid=11002,
                                 endpoint=apps_endpoint)["ok"])


if __name__ == "__main__":
    unittest.main()
