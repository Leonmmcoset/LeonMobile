"""Real Unix credentials, durable authentication and PTY privilege sessions.

rootd --test uses private state/proc/module fixtures; host management operations
are unavailable. Authenticated test shells only inspect their own credentials.
"""
import array
import contextlib
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import signal
import socket
import subprocess
import tempfile
import termios
import threading
import time
import unittest
from test_system_services import rpc

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))
SECRET = "LeonRoot!2026"


@unittest.skipUnless(os.geteuid() == 0, "root credential tests require root")
class RootAccess(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leon-root-")
        self.root = Path(self.temp.name)
        self.root.chmod(0o755)
        self.state = self.root / "state"
        self.endpoint = self.root / "runtime/root.sock"
        (self.root / "proc").mkdir()
        (self.root / "modules").mkdir()
        (self.root / "services").mkdir()
        self.process = None
        self.start()

    def start(self):
        self.process = subprocess.Popen([BUILD / "bin/rootd", "--test", "--state-dir", self.state,
                                         "--socket", self.endpoint, "--proc", self.root / "proc",
                                         "--modules", self.root / "modules", "--services", self.root / "services",
                                         "--service-socket", self.root / "manager.sock"],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.fail(self.process.stderr.read().decode())
            try:
                rpc(self.endpoint, {"op": "status"})
                return
            except (OSError, ValueError):
                time.sleep(.02)
        self.fail("rootd did not start")

    def stop(self):
        self.process.terminate()
        out, error = self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 0, error.decode())
        self.assertNotIn(SECRET.encode(), out + error)

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.stop()
        self.temp.cleanup()

    def call(self, op, uid=11000, **fields):
        return rpc(self.endpoint, dict(op=op, **fields), uid)

    def enable(self):
        result = self.call("enable", password=SECRET)
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["enabled"])

    def test_disabled_default_and_identity_boundaries(self):
        self.assertEqual(self.endpoint.parent.stat().st_mode & 0o777, 0o755)
        state = self.call("status", uid=1000)
        self.assertFalse(state["enabled"])
        self.assertFalse(state["password_set"])
        self.assertFalse(state["sudo_enabled"])
        for uid in (1000, 11005, 11001, 12000):
            self.assertFalse(self.call("enable", uid=uid, password=SECRET)["ok"])
        self.assertFalse(self.call("exec", uid=1000, password=SECRET)["ok"])
        self.assertFalse(self.call("enable", password="weak")["ok"])
        self.enable()
        for uid in (11001, 12000, 11000):
            self.assertFalse(rpc(self.endpoint, dict(op="exec", password=SECRET, uid=0), uid)["ok"])

    def test_hash_permissions_persistence_and_fail_closed_state(self):
        self.enable()
        policy = self.state / "policy.json"
        self.assertNotIn(SECRET, policy.read_text())
        self.assertEqual(policy.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.state.stat().st_mode & 0o777, 0o700)
        original = json.loads(policy.read_text())
        self.stop()
        self.start()
        self.assertTrue(self.call("status")["enabled"])
        self.assertEqual(json.loads(policy.read_text()), original)
        self.stop()
        policy.write_text('{"enabled":true}')
        self.start()
        self.assertFalse(self.call("status")["ok"])
        self.assertFalse(self.call("enable", password=SECRET)["ok"])

    def test_password_cooldown_survives_restart_and_rotation_revokes_old_password(self):
        self.enable()
        self.assertFalse(self.call("disable", password="incorrect")["ok"])
        self.stop()
        self.start()
        self.assertGreater(self.call("status")["retry_after_ms"], 0)
        self.assertFalse(self.call("disable", password=SECRET)["ok"])
        time.sleep(1.05)
        changed = self.call("password", password=SECRET, new_password="AnotherRoot!2026")
        self.assertTrue(changed["ok"], changed)
        self.assertFalse(self.call("disable", password=SECRET)["ok"])
        time.sleep(1.05)
        self.assertTrue(self.call("disable", password="AnotherRoot!2026")["ok"])
        self.assertFalse(self.call("enable", password="brand-new-reset")["ok"])

    @contextlib.contextmanager
    def session(self, command):
        connection = socket.socket(socket.AF_UNIX)
        connection.settimeout(5)
        connection.connect(str(self.endpoint))
        connection.sendall(json.dumps(dict(op="exec", password=SECRET, command=command,
                                           rows=31, columns=92)).encode() + b"\n")
        master = -1
        data = bytearray()
        while not data.endswith(b"\n"):
            block, controls, _, _ = connection.recvmsg(1, socket.CMSG_SPACE(4), socket.MSG_CMSG_CLOEXEC)
            self.assertTrue(block)
            data.extend(block)
            for level, kind, value in controls:
                self.assertEqual((level, kind), (socket.SOL_SOCKET, socket.SCM_RIGHTS))
                master = array.array("i", value)[0]
        response = json.loads(data)
        self.assertTrue(response["ok"], response)
        self.assertGreaterEqual(master, 0)
        try:
            yield connection, master, response
        finally:
            os.close(master)
            connection.close()

    @staticmethod
    def read_terminal(master, marker):
        data = bytearray()
        deadline = time.monotonic() + 5
        while marker not in data and time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                try:
                    block = os.read(master, 65536)
                except OSError:
                    break
                if not block:
                    break
                data.extend(block)
        return bytes(data)

    def test_root_pty_real_uid_window_size_and_exact_exit_status(self):
        self.enable()
        with self.session("id -u; stty size; printf ROOT_READY; exit 7") as (connection, master, reply):
            output = self.read_terminal(master, b"ROOT_READY")
            self.assertIn(b"0\r\n31 92\r\nROOT_READY", output)
            result = json.loads(connection.makefile("rb").readline())
            self.assertEqual(result["status"], 7)

    def test_disable_terminates_live_root_session_and_keeps_password(self):
        self.enable()
        with self.session("printf ROOT_READY; exec sleep 60") as (connection, master, reply):
            self.assertIn(b"ROOT_READY", self.read_terminal(master, b"ROOT_READY"))
            self.assertTrue(self.call("disable", password=SECRET)["ok"])
            deadline = time.monotonic() + 3
            while Path(f"/proc/{reply['pid']}").exists() and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertFalse(Path(f"/proc/{reply['pid']}").exists())
        self.assertFalse(self.call("status")["enabled"])
        self.assertTrue(self.call("status")["password_set"])
        self.assertTrue(self.call("enable", password=SECRET)["ok"])

    def test_password_rotation_and_disconnect_revoke_sessions(self):
        self.enable()
        with self.session("printf ROOT_READY; exec sleep 60") as (connection, master, reply):
            self.assertIn(b"ROOT_READY", self.read_terminal(master, b"ROOT_READY"))
            self.assertTrue(self.call("password", password=SECRET, new_password="AnotherRoot!2026")["ok"])
            deadline = time.monotonic() + 3
            while Path(f"/proc/{reply['pid']}").exists() and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertFalse(Path(f"/proc/{reply['pid']}").exists())
        self.assertTrue(self.call("password", password="AnotherRoot!2026", new_password=SECRET)["ok"])
        with self.session("printf ROOT_READY; exec sleep 60") as (connection, master, reply):
            self.assertIn(b"ROOT_READY", self.read_terminal(master, b"ROOT_READY"))
            connection.close()
            deadline = time.monotonic() + 3
            while Path(f"/proc/{reply['pid']}").exists() and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertFalse(Path(f"/proc/{reply['pid']}").exists())

    def test_unsafe_policy_permissions_links_and_extended_lockout(self):
        self.enable()
        policy = self.state / "policy.json"
        policy.chmod(0o644)
        self.assertFalse(self.call("status")["ok"])
        policy.chmod(0o600)
        os.link(policy, self.state / "second-link")
        self.assertFalse(self.call("disable", password=SECRET)["ok"])
        (self.state / "second-link").unlink()
        for attempt in range(5):
            self.assertFalse(self.call("disable", password="wrong-password")["ok"])
            if attempt < 4:
                time.sleep(1.05)
        self.assertGreater(self.call("status")["retry_after_ms"], 28000)
        self.stop()
        self.start()
        self.assertFalse(self.call("disable", password=SECRET)["ok"])
        self.assertGreater(self.call("status")["retry_after_ms"], 27000)

    def test_session_limit_preserves_control_and_revocation_capacity(self):
        self.enable()
        with contextlib.ExitStack() as stack:
            sessions = [stack.enter_context(self.session("printf ROOT_READY; exec sleep 60")) for _ in range(6)]
            for connection, master, reply in sessions:
                self.assertIn(b"ROOT_READY", self.read_terminal(master, b"ROOT_READY"))
            rejected = self.call("exec", uid=0, password=SECRET, command="id -u")
            self.assertFalse(rejected["ok"])
            self.assertIn("limit", rejected["error"])
            self.assertTrue(self.call("status")["ok"])
            self.assertTrue(self.call("disable", password=SECRET)["ok"])
            deadline = time.monotonic() + 3
            while any(Path(f"/proc/{reply['pid']}").exists() for _, _, reply in sessions) and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertFalse(any(Path(f"/proc/{reply['pid']}").exists() for _, _, reply in sessions))

    def test_atomic_policy_replacement_preserves_live_sessions(self):
        self.enable()
        with self.session("printf ROOT_READY; exec sleep 60") as (_, master, reply):
            self.assertIn(b"ROOT_READY", self.read_terminal(master, b"ROOT_READY"))
            policy = self.state / "policy.json"
            snapshot = policy.read_bytes()
            stop = threading.Event()
            errors = []
            replacements = 0

            def replace_policy():
                nonlocal replacements
                try:
                    while not stop.is_set():
                        next_policy = self.state / "policy-test-next"
                        fd = os.open(next_policy, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                        with os.fdopen(fd, "wb") as output:
                            output.write(snapshot)
                        os.replace(next_policy, policy)
                        replacements += 1
                except BaseException as error:
                    errors.append(error)

            writer = threading.Thread(target=replace_policy)
            writer.start()
            try:
                # Exercise actual rootd readers while a trusted writer publishes
                # complete, unchanged policy generations using atomic rename.
                states = [self.call("status", uid=0) for _ in range(512)]
                self.assertTrue(all(state.get("ok") for state in states),
                                [state for state in states if not state.get("ok")])
            finally:
                stop.set()
                writer.join(timeout=5)
            self.assertFalse(writer.is_alive())
            self.assertFalse(errors)
            self.assertGreater(replacements, 0)
            time.sleep(.2)
            self.assertTrue(Path(f"/proc/{reply['pid']}").exists())
            self.assertTrue(self.call("disable", password=SECRET)["ok"])
            deadline = time.monotonic() + 3
            while Path(f"/proc/{reply['pid']}").exists() and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertFalse(Path(f"/proc/{reply['pid']}").exists())

    def test_su_client_password_not_echoed_and_terminal_restored(self):
        self.enable()
        pid, master = pty.fork()
        if not pid:
            os.environ["LEON_ROOT_SOCKET"] = str(self.endpoint)
            os.execv(str(BUILD / "bin/leonsu"), ["su", "-c", "id -u; exit 7"])
        try:
            original = termios.tcgetattr(master)
            output = self.read_terminal(master, b"Root password: ")
            self.assertIn(b"Root password: ", output)
            os.write(master, SECRET.encode() + b"\n")
            output += self.read_terminal(master, b"0\r\n")
            self.assertNotIn(SECRET.encode(), output)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                reaped, status = os.waitpid(pid, os.WNOHANG)
                if reaped:
                    break
                time.sleep(.02)
            else:
                self.fail("su did not finish")
            self.assertEqual(os.waitstatus_to_exitcode(status), 7)
            self.assertEqual(termios.tcgetattr(master)[3], original[3])
        finally:
            try:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            except ProcessLookupError:
                pass
            os.close(master)

    def test_kernel_paths_and_authenticated_runtime_change(self):
        folder = self.root / "proc/sys/kernel"
        folder.mkdir(parents=True)
        parameter = folder / "hostname"
        parameter.write_text("before\n")
        (folder / "escape").symlink_to(self.root / "outside")
        self.assertFalse(self.call("sysctl", area="kernel", path="kernel/hostname", value="after", password=SECRET)["ok"])
        self.enable()
        self.assertEqual(self.call("inspect", area="kernel", path="kernel/hostname")["value"], "before")
        self.assertTrue(self.call("sysctl", area="kernel", path="kernel/hostname", value="after", password=SECRET)["ok"])
        self.assertEqual(parameter.read_text(), "after\n")
        for path in ("../outside", "/etc/passwd", "kernel/escape", "kernel/../hostname"):
            self.assertFalse(self.call("inspect", area="kernel", path=path)["ok"])
            self.assertFalse(self.call("sysctl", area="kernel", path=path, value="x", password=SECRET)["ok"])
        self.assertFalse(self.call("sysctl", area="kernel", path="kernel/hostname", value="bad\nvalue", password=SECRET)["ok"])
        self.assertEqual(parameter.read_text(), "after\n")

    def test_service_control_and_durable_boot_restart_policy(self):
        service = self.root / "services/provider.toml"
        service.write_text('name="provider"\ncommand=["/bin/sleep","60"]\nrestart="never"\n')
        policy = self.state / "services"
        manager = subprocess.Popen([BUILD / "bin/leond", "--config", self.root / "services", "--socket", self.root / "manager.sock",
                                    "--log-dir", self.root / "logs", "--policy-dir", policy], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while not (self.root / "manager.sock").exists() and time.monotonic() < deadline: time.sleep(.02)
            self.enable()
            status = self.call("inspect", area="services", name="provider")
            self.assertTrue(status["ok"], status)
            self.assertEqual(status["restart"], "never")
            self.assertTrue(self.call("service_config", area="services", name="provider", password=SECRET,
                                      autostart=False, restart="on-failure", restart_limit=7)["ok"])
            status = self.call("inspect", area="services", name="provider")
            self.assertFalse(status["autostart"])
            self.assertEqual(status["restart_limit"], 7)
            self.assertEqual((policy / "provider.json").stat().st_mode & 0o777, 0o600)
            for action in ("stop", "start", "restart"):
                self.assertTrue(self.call("service_control", area="services", name="provider", action=action, password=SECRET)["ok"])
            self.assertFalse(self.call("service_control", area="services", name="../root", action="stop", password=SECRET)["ok"])
            self.assertFalse(self.call("service_config", area="services", name="provider", password=SECRET,
                                       autostart="false", restart="always", restart_limit=7)["ok"])
            manager.terminate(); manager.communicate(timeout=5)
            marker = self.root / "autostarted"
            service.write_text('name="provider"\ncommand=["/bin/touch",' + json.dumps(str(marker)) + ']\ntype="oneshot"\n')
            manager = subprocess.Popen([BUILD / "bin/leond", "--config", self.root / "services", "--socket", self.root / "manager.sock",
                                        "--log-dir", self.root / "logs", "--policy-dir", policy], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(.3)
            self.assertFalse(marker.exists())
        finally:
            if manager.poll() is None: manager.terminate()
            manager.communicate(timeout=5)

    def test_transport_slow_clients_and_scm_descriptor_rejection(self):
        self.enable()
        slow = socket.socket(socket.AF_UNIX)
        slow.connect(str(self.endpoint))
        slow.sendall(b'{"op":')
        try:
            self.assertTrue(self.call("status")["ok"])
            for data in (b'{}\x00\n', b'{"op":"status","password":"bad\\u0000value"}\n', b'{' + b'x' * 8192 + b'\n'):
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(5); client.connect(str(self.endpoint)); client.sendall(data)
                    try: client.recv(8192)
                    except ConnectionResetError: pass
            with socket.socket(socket.AF_UNIX) as client:
                client.settimeout(5); client.connect(str(self.endpoint))
                fd = os.open('/dev/null', os.O_RDONLY)
                try: client.sendmsg([b'{"op":"status"}\n'], [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array('i', [fd]))])
                finally: os.close(fd)
                try: self.assertFalse(client.recv(8192))
                except ConnectionResetError: pass
            self.assertTrue(self.call("status")["enabled"])
        finally: slow.close()
