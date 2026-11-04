"""Ready dependencies, peer PID authentication, watchdog and bounded recovery."""
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


@unittest.skipUnless(os.geteuid() == 0, "service credentials require root")
class WatchdogTests(unittest.TestCase):
    def run_fixture(self, body, assertions, timeout=7):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            helper = root / "provider.py"
            helper.write_text("import os,socket,time\nfrom pathlib import Path\n"
                              "s=socket.socket(fileno=int(os.environ['LEON_NOTIFY_FD']))\n"
                              f"root=Path({str(root)!r})\n" + body)
            argv = json.dumps([sys.executable, str(helper)])
            (root / "provider.toml").write_text(
                'name="provider"\ntype="notify"\nready_timeout_ms=500\nwatchdog_ms=500\n'
                'restart="on-failure"\nrestart_limit=1\n' + f"command={argv}\n")
            (root / "client.toml").write_text(
                'name="client"\ntype="oneshot"\nafter=["provider"]\ncommand=["/bin/touch",'
                + json.dumps(str(root / "client")) + ']\n')
            proc = subprocess.Popen([BUILD / "bin/leond", "--config", root, "--socket",
                                     root / "s.sock", "--log-dir", root / "logs"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                deadline = time.monotonic() + timeout
                state = ""
                while time.monotonic() < deadline:
                    if (root / "s.sock").exists():
                        with socket.socket(socket.AF_UNIX) as client:
                            client.settimeout(2)
                            client.connect(str(root / "s.sock"))
                            client.sendall(b"status\n")
                            data = bytearray()
                            while block := client.recv(4096):
                                data.extend(block)
                            state = data.decode()
                        if assertions(root, state):
                            return
                    time.sleep(.03)
                self.fail("Fixture did not reach expected health: " + state)
            finally:
                proc.send_signal(signal.SIGTERM)
                proc.communicate(timeout=5)

    def test_ready_dependency_and_watchdog(self):
        def check(root, state):
            if not (root / "ready").exists():
                self.assertFalse((root / "client").exists())
            return (root / "client").exists() and "health=watchdog-timeout" in state and "exit=125" in state
        self.run_fixture("time.sleep(.2)\n(root/'ready').touch()\ns.send(b'READY=1')\n"
                         "for i in range(4):\n s.send(b'WATCHDOG=1'); time.sleep(.1)\n"
                         "time.sleep(5)\n", check)

    def test_descendant_cannot_forge_ready(self):
        def check(root, state):
            self.assertFalse((root / "client").exists())
            return "exit=124" in state and "health=ready-timeout" in state
        self.run_fixture("if os.fork()==0:\n s.send(b'READY=1'); time.sleep(5); os._exit(0)\n"
                         "time.sleep(5)\n", check)

    def test_healthy_heartbeat(self):
        self.run_fixture("s.send(b'READY=1')\nend=time.monotonic()+3\n"
                         "while time.monotonic()<end:\n s.send(b'WATCHDOG=1'); time.sleep(.1)\n"
                         "(root/'healthy').touch()\ntime.sleep(5)\n",
                         lambda root, state: (root / "healthy").exists() and
                         (root / "client").exists() and "health=ready" in state and "retries=0" in state)
