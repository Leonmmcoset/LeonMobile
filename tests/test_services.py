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
BIN = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


@unittest.skipUnless(os.geteuid() == 0, "supervisor uses root service credentials")
class SupervisorTests(unittest.TestCase):
    def test_dependency_remains_available_during_client_shutdown(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            helper = path / "service.py"
            helper.write_text(
                "import pathlib,signal,sys,time\n"
                "root=pathlib.Path(sys.argv[1]); role=sys.argv[2]\n"
                "def shutdown(sig,frame):\n"
                "    if role == 'client':\n"
                "        time.sleep(.3)\n"
                "        result='client:' + ('available' if (root/'provider.ready').exists() else 'lost')\n"
                "    else:\n"
                "        (root/'provider.ready').unlink()\n"
                "        result='provider'\n"
                "    with (root/'stops').open('a') as stream: stream.write(result+'\\n')\n"
                "    sys.exit(0)\n"
                "signal.signal(signal.SIGTERM,shutdown)\n"
                "(root/(role+'.ready')).touch()\n"
                "while True: time.sleep(1)\n")
            for role in ("provider", "client"):
                command = json.dumps([sys.executable, str(helper), str(path), role])
                dependency = 'after=["provider"]\n' if role == "client" else ""
                (path / (role + ".toml")).write_text(
                    f'name="{role}"\ncommand={command}\n' + dependency)
            proc = subprocess.Popen([BIN / "src/system/leond", "--config", path,
                                     "--socket", path / "s.sock", "--log-dir", path / "logs"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if all((path / (role + ".ready")).exists() for role in ("provider", "client")):
                        break
                    time.sleep(.02)
                else:
                    self.fail("Services did not become ready")
                proc.send_signal(signal.SIGTERM)
                proc.communicate(timeout=5)
                self.assertEqual(proc.returncode, 0)
                self.assertEqual((path / "stops").read_text().splitlines(),
                                 ["client:available", "provider"])
            finally:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM)
                    proc.communicate(timeout=5)

    def test_oneshot_order_restart_limit_and_status(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            ready = path / "ready"
            attempts = path / "attempts"
            (path / "prepare.toml").write_text(
                'name="prepare"\ntype="oneshot"\ncommand=["/bin/sh","-c",'
                + json.dumps(f"sleep .2; touch {ready}") + ']\n')
            (path / "child.toml").write_text(
                'name="child"\nafter=["prepare"]\nrestart="on-failure"\nrestart_limit=1\n'
                'command=["/bin/sh","-c",' + json.dumps(f"echo attempt >> {attempts}; test -f {ready} && exit 7; exit 8") + ']\n')
            endpoint = str(path / "s.sock")
            proc = subprocess.Popen([BIN / "src/system/leond", "--config", path, "--socket", endpoint,
                                     "--log-dir", path / "logs"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                status = ""
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    if Path(endpoint).exists():
                        with socket.socket(socket.AF_UNIX) as client:
                            client.connect(endpoint)
                            client.sendall(b"status\n")
                            pieces = []
                            while piece := client.recv(4096):
                                pieces.append(piece)
                            status = b"".join(pieces).decode()
                            if "child\tfailed" in status and attempts.exists() and len(attempts.read_text().splitlines()) == 2:
                                break
                    time.sleep(.1)
                self.assertIn("prepare\tfinished", status)
                self.assertIn("child\tfailed", status)
                self.assertIn("exit=7", status)
                self.assertIn("retries=1", status)
                time.sleep(.3)
                self.assertEqual(len(attempts.read_text().splitlines()), 2)
            finally:
                proc.send_signal(signal.SIGTERM)
                proc.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
