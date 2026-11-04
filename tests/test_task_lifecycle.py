"""Exercise asynchronous task dismissal against real application identities."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


@unittest.skipUnless(os.geteuid() == 0, "application identities require root")
class TaskLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root.chmod(0o755)
        builtin = self.root / "builtin"
        builtin.mkdir(mode=0o755)
        builtin.chmod(0o755)
        (self.root / "data").mkdir()
        (self.root / "data").chmod(0o755)
        helper = self.root / "client.py"
        helper.write_text(textwrap.dedent("""\
            import os, pathlib, signal, socket, sys, time
            home = pathlib.Path(os.environ['HOME'])
            mode = sys.argv[1]
            if mode == 'stall':
                signal.signal(signal.SIGTERM, signal.SIG_IGN)
            (home / 'ready').touch()
            if mode == 'legacy':
                while True:
                    time.sleep(60)
            channel = socket.socket(fileno=int(os.environ['LEON_LIFECYCLE_FD']))
            channel.setblocking(True)
            while packet := channel.recv(128):
                seq, event = packet.decode().split()
                with (home / 'events').open('a') as stream:
                    stream.write(event + '\\n')
                if event in ('background', 'save', 'shutdown'):
                    (home / 'checkpoint').write_text(event)
                channel.send((seq + ' ok').encode())
                if event == 'shutdown' and mode != 'stall':
                    break
            """))
        helper.chmod(0o644)
        for name, mode in (("gallery", "cooperate"), ("calculator", "stall"),
                           ("clock", "stall"), ("editor", "cooperate"), ("files", "legacy")):
            manifest = dict(format=1, id="org.leon." + name, version="1.0.0", arch="aarch64",
                            lmui=1, name=name, entry=sys.executable, args=[str(helper), mode])
            if mode != "legacy":
                manifest["lifecycle"] = 1
            (builtin / (name + ".json")).write_text(json.dumps(manifest))
        self.endpoint = self.root / "apps.sock"
        self.proc = subprocess.Popen([BUILD / "bin/leon-launchd", "--root", self.root,
                                      "--builtin", builtin, "--socket", self.endpoint,
                                      "--runtime-dir", self.root / "runtime"],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self.shutdown)
        deadline = time.monotonic() + 5
        while not self.endpoint.exists() and time.monotonic() < deadline:
            self.assertIsNone(self.proc.poll())
            time.sleep(.01)
        self.assertTrue(self.endpoint.exists())

    def shutdown(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        _, error = self.proc.communicate(timeout=6)
        self.assertEqual(self.proc.returncode, 0, error.decode())

    def rpc(self, message, uid=0):
        if uid:
            code = ("import socket,sys; s=socket.socket(socket.AF_UNIX); s.settimeout(2); "
                    "s.connect(sys.argv[1]); s.sendall(sys.argv[2].encode()+b'\\n'); "
                    "s.shutdown(socket.SHUT_WR); sys.stdout.buffer.write(s.makefile('rb').read())")
            result = subprocess.run([sys.executable, "-c", code, str(self.endpoint),
                                     json.dumps(message)], user=uid, group=uid, extra_groups=[],
                                    capture_output=True, timeout=3, check=True)
            return json.loads(result.stdout)
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(2)
            client.connect(str(self.endpoint))
            client.sendall(json.dumps(message).encode() + b"\n")
            client.shutdown(socket.SHUT_WR)
            return json.load(client.makefile("r"))

    def launch(self, name):
        app_id = "org.leon." + name
        response = self.rpc({"op": "launch", "id": app_id})
        self.assertTrue(response["ok"], response)
        deadline = time.monotonic() + 3
        while not (self.root / "data" / app_id / "ready").exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue((self.root / "data" / app_id / "ready").exists(),
                        (self.root / "data" / app_id / "application.log").read_text())
        return response["pid"]

    def wait_exit(self, app_id, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            response = self.rpc({"op": "lifecycle"})
            if app_id not in response["applications"]:
                return response
            time.sleep(.02)
        self.fail("task did not exit: " + str(response))

    def test_cooperative_close_saves_and_preserves_the_other_foreground_app(self):
        app_id = "org.leon.gallery"
        pid = self.launch("gallery")
        self.assertTrue(self.rpc({"op": "background"})["ok"])
        self.assertEqual(self.rpc({"op": "launch", "id": app_id})["pid"], pid)
        editor_pid = self.launch("editor")
        states = self.rpc({"op": "list"})["applications"]
        self.assertFalse(states[app_id]["foreground"])
        self.assertTrue(states["org.leon.editor"]["foreground"])
        begin = time.monotonic()
        self.assertTrue(self.rpc({"op": "close", "id": app_id}, 1000)["ok"])
        self.assertLess(time.monotonic() - begin, .8)
        response = self.wait_exit(app_id)
        editor = response["applications"]["org.leon.editor"]
        self.assertTrue(editor["foreground"])
        self.assertEqual(editor["pid"], editor_pid)
        home = self.root / "data" / app_id
        self.assertEqual((home / "checkpoint").read_text(), "shutdown")
        self.assertEqual((home / "events").read_text().splitlines(),
                         ["foreground", "background", "foreground", "background", "shutdown"])
        self.assertEqual([exit for exit in response["exits"] if exit["pid"] == pid],
                         [{"app": app_id, "pid": pid, "status": 0}])
        self.assertTrue(self.rpc({"op": "close", "id": app_id})["ok"])

    def test_parallel_close_keeps_broker_responsive_and_does_not_reset_deadlines(self):
        ids = ("org.leon.calculator", "org.leon.clock")
        pids = {app_id: self.launch(app_id.rsplit(".", 1)[1]) for app_id in ids}
        begin = time.monotonic()
        for app_id in ids:
            self.assertTrue(self.rpc({"op": "close", "id": app_id})["ok"])
        self.assertLess(time.monotonic() - begin, .8)
        self.launch("editor")
        for op in ("foreground", "launch", "save"):
            response = self.rpc({"op": op, "id": ids[0]})
            self.assertFalse(response["ok"], response)
            self.assertEqual(response["error"], "application is stopping")
        saw_term = saw_shutdown_ack = False
        while time.monotonic() - begin < 4.5:
            for app_id in ids:
                self.assertTrue(self.rpc({"op": "close", "id": app_id})["ok"])
            response = self.rpc({"op": "lifecycle"})
            states = response["applications"]
            self.assertTrue(states["org.leon.editor"]["foreground"])
            for app_id in ids:
                if app_id in states:
                    state = states[app_id]
                    self.assertTrue(state["stopping"])
                    self.assertFalse(state["foreground"])
                    saw_term |= state["stop_phase"] == "term"
                    saw_shutdown_ack |= state["event"] == "shutdown" and not state["pending"]
            if not any(app_id in states for app_id in ids):
                break
            time.sleep(.05)
        else:
            self.fail("repeated close requests delayed termination")
        self.assertGreaterEqual(time.monotonic() - begin, 2.8)
        self.assertTrue(saw_term)
        self.assertTrue(saw_shutdown_ack)
        for app_id, pid in pids.items():
            self.assertEqual([exit for exit in response["exits"] if exit["pid"] == pid],
                             [{"app": app_id, "pid": pid, "status": 128 + signal.SIGKILL}])
            with self.assertRaises(ProcessLookupError):
                os.kill(pid, 0)

    def test_legacy_close_uses_signals_without_checkpoint_grace(self):
        pid = self.launch("files")
        begin = time.monotonic()
        self.assertTrue(self.rpc({"op": "close", "id": "org.leon.files"})["ok"])
        response = self.wait_exit("org.leon.files", timeout=1.5)
        self.assertLess(time.monotonic() - begin, 1.5)
        self.assertIn({"app": "org.leon.files", "pid": pid, "status": 128 + signal.SIGTERM},
                      response["exits"])

    def test_application_cannot_close_another_application(self):
        pid = self.launch("gallery")
        response = self.rpc({"op": "close", "id": "org.leon.gallery"}, 11004)
        self.assertFalse(response["ok"], response)
        self.assertEqual(response["error"], "permission denied")
        state = self.rpc({"op": "lifecycle"})["applications"]["org.leon.gallery"]
        self.assertEqual(state["pid"], pid)
        self.assertTrue(state["foreground"])
        self.assertFalse(state["stopping"])
