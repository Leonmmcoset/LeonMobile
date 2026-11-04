"""Exercise application transitions, cooperative exits and timeout escalation."""
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


@unittest.skipUnless(os.geteuid() == 0, "application identities require root")
class LifecycleTests(unittest.TestCase):
    def test_transitions_checkpoint_and_bounded_shutdown(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            root.chmod(0o755)
            builtin = root / "builtin"
            builtin.mkdir(mode=0o755)
            builtin.chmod(0o755)
            (root / "data").mkdir(mode=0o755)
            (root / "data").chmod(0o755)
            endpoint = root / "apps.sock"
            helper = root / "client.py"
            helper.write_text(
                "import os,socket,pathlib,signal,time,sys\n"
                "s=socket.socket(fileno=int(os.environ['LEON_LIFECYCLE_FD'])); s.setblocking(True)\n"
                "home=pathlib.Path(os.environ['HOME'])\n"
                "if sys.argv[1]=='hang':\n signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)\n"
                "while True:\n"
                " packet=s.recv(128).decode(); seq,event=packet.split()\n"
                " with (home/'events').open('a') as f: f.write(event+'\\n')\n"
                " if event in ('background','shutdown'): (home/'checkpoint').write_text('saved')\n"
                " s.send((seq+' ok').encode())\n"
                " if event=='shutdown': break\n")
            helper.chmod(0o644)
            for name, mode in (("gallery", "cooperate"), ("calculator", "hang")):
                (builtin / (name + ".json")).write_text(json.dumps(dict(
                    format=1, id="org.leon." + name, version="1.0.0", arch="aarch64", lmui=1,
                    lifecycle=1, name=name, entry=sys.executable, args=[str(helper), mode])))
            proc = subprocess.Popen([BUILD / "bin/leon-launchd", "--root", root,
                                     "--builtin", builtin, "--socket", endpoint,
                                     "--runtime-dir", root / "runtime"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            def rpc(message):
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(6)
                    client.connect(str(endpoint))
                    client.sendall(json.dumps(message).encode() + b"\n")
                    out = bytearray()
                    while block := client.recv(4096): out.extend(block)
                    return json.loads(out)
            try:
                deadline = time.monotonic() + 5
                while not endpoint.exists() and time.monotonic() < deadline: time.sleep(.02)
                response = rpc({"op":"launch", "id":"org.leon.gallery"})
                self.assertTrue(response["ok"], response)
                gallery = root / "data/org.leon.gallery"
                while not (gallery / "events").exists() and time.monotonic() < deadline: time.sleep(.02)
                self.assertTrue((gallery / "events").exists(), (gallery / "application.log").read_text())
                self.assertTrue(rpc({"op":"background"})["ok"])
                while not (gallery / "checkpoint").exists() and time.monotonic() < deadline: time.sleep(.02)
                self.assertEqual((gallery / "checkpoint").read_text(), "saved")
                self.assertTrue(rpc({"op":"foreground", "id":"org.leon.gallery"})["ok"])
                self.assertTrue(rpc({"op":"launch", "id":"org.leon.calculator"})["ok"])
                time.sleep(2.4)
                state = rpc({"op":"lifecycle"})["applications"]
                self.assertFalse(state["org.leon.gallery"]["foreground"])
                self.assertTrue(state["org.leon.calculator"]["foreground"])
                self.assertFalse(state["org.leon.calculator"]["responsive"])
                self.assertTrue(rpc({"op":"stop", "id":"org.leon.gallery"})["ok"])
                self.assertIn("shutdown", (gallery / "events").read_text())
                begin = time.monotonic()
                self.assertTrue(rpc({"op":"stop", "id":"org.leon.calculator"})["ok"])
                self.assertLess(time.monotonic() - begin, 4.5)
                self.assertEqual(rpc({"op":"list"})["running"], {})
            finally:
                proc.terminate()
                proc.communicate(timeout=5)
