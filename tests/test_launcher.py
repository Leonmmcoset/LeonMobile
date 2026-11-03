"""Verify real UID transitions and peer authorization, using isolated paths."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


@unittest.skipUnless(os.geteuid() == 0, "launcher identity transitions require root")
class LauncherTests(unittest.TestCase):
    def test_private_data_and_peer_authorization(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            path.chmod(0o755)
            root = path / "root"
            root.mkdir(mode=0o755)
            root.chmod(0o755)
            builtin = path / "builtin"
            builtin.mkdir(mode=0o755)
            builtin.chmod(0o755)
            runtime = path / "runtime"
            runtime.mkdir(mode=0o755)
            runtime.chmod(0o755)
            private = root / "data/org.leon.calculator"
            private.mkdir(parents=True)
            private.parent.chmod(0o755)
            private.chmod(0o700)
            secret = private / "secret"
            secret.write_text("private")
            os.chown(private, 11001, 11001)
            os.chown(secret, 11001, 11001)
            script = (
                "import os,json,pathlib; home=pathlib.Path(os.environ['HOME']); "
                f"denied=not os.access({str(secret)!r},os.R_OK); "
                "(home/'result').write_text(json.dumps({'uid':os.getuid(),'denied':denied})); "
                "__import__('time').sleep(30)"
            )
            manifest = dict(format=1, id="org.leon.gallery", version="1.0.0", arch="aarch64",
                            lmui=1, name="Test", entry="/usr/bin/python3", args=["-c", script])
            m = builtin / "gallery.json"
            m.write_text(json.dumps(manifest))
            m.chmod(0o644)
            # Existing stores predate the optional motion preference.
            (root / "settings.json").write_text(json.dumps(dict(
                dark=False, language="zh-CN", navigation="gestures", volume=70)))
            endpoint = str(path / "apps.sock")
            proc = subprocess.Popen([BIN / "bin/leon-launchd", "--root", root,
                                     "--builtin", builtin, "--socket", endpoint,
                                     "--runtime-dir", runtime], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)

            def rpc(message):
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(3)
                    client.connect(endpoint)
                    client.sendall(json.dumps(message).encode() + b"\n")
                    out = bytearray()
                    while block := client.recv(4096):
                        out.extend(block)
                    return json.loads(out)

            try:
                deadline = time.monotonic() + 5
                while not Path(endpoint).exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertIs(rpc({"op": "settings"})["settings"]["reduce_motion"], False)
                self.assertTrue(rpc({"op": "set", "key": "reduce_motion", "value": True})["ok"])
                self.assertIs(json.loads((root / "settings.json").read_text())["reduce_motion"], True)
                for invalid in (1, "true", None):
                    self.assertFalse(rpc({"op": "set", "key": "reduce_motion", "value": invalid})["ok"])
                self.assertTrue(rpc({"op": "set", "key": "reduce_motion", "value": False})["ok"])
                self.assertTrue(rpc({"op": "launch", "id": "org.leon.gallery"})["ok"])
                result = root / "data/org.leon.gallery/result"
                while not result.exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                log = result.parent / "application.log"
                self.assertTrue(result.exists(), "application did not start: " +
                                (log.read_text() if log.exists() else "no log"))
                self.assertEqual(json.loads(result.read_text()), {"uid": 11002, "denied": True})
                home = result.parent
                self.assertEqual(home.stat().st_mode & 0o777, 0o700)

                def drop_identity():
                    os.setgroups([])
                    os.setgid(11002)
                    os.setuid(11002)

                request = json.dumps({"op": "set", "key": "dark", "value": True})
                probe = (
                    "import socket; s=socket.socket(socket.AF_UNIX); "
                    f"s.connect({endpoint!r}); s.sendall({(request + chr(10)).encode()!r}); "
                    "print(s.recv(4096).decode())"
                )
                denied = subprocess.run(["python3", "-c", probe], capture_output=True,
                                        text=True, preexec_fn=drop_identity, timeout=5)
                self.assertFalse(json.loads(denied.stdout)["ok"])
                self.assertFalse(rpc({"op": "settings"})["settings"]["dark"])
                self.assertFalse(rpc({"op": "poweroff"})["ok"])
                self.assertTrue(rpc({"op": "stop", "id": "org.leon.gallery"})["ok"])
            finally:
                proc.send_signal(signal.SIGTERM)
                proc.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
