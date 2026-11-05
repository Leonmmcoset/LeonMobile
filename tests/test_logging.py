"""Real AF_UNIX credentials, persistent quotas and failure isolation for logd."""
import array
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

BIN = Path(os.environ.get("LEON_TEST_BIN", Path(__file__).resolve().parents[1] / "build/host")) / "bin"
MAGIC = 0x4c4d4c31
BOOT_A = "00000000-0000-0000-0000-000000000001"
BOOT_B = "00000000-0000-0000-0000-000000000002"


def packet(message, *, uid=12000, pid=123, scope=2, level=6, source="org.example.logs"):
    if isinstance(message, str):
        message = message.encode()
    return struct.pack("=6I96s1024s", MAGIC, uid, pid, level, scope, len(message),
                       source.encode(), message)


def as_uid(uid, action):
    reader, writer = os.pipe()
    pid = os.fork()
    if not pid:
        os.close(reader)
        try:
            os.setgroups([])
            os.setgid(uid)
            os.setuid(uid)
            result = {"value": action()}
        except Exception as error:
            result = {"error": str(error)}
        data = json.dumps(result).encode()
        while data:
            n = os.write(writer, data)
            data = data[n:]
        os._exit(0)
    os.close(writer)
    with os.fdopen(reader, "rb") as stream:
        result = json.loads(stream.read())
    os.waitpid(pid, 0)
    if "error" in result:
        raise AssertionError(result["error"])
    return result["value"]


@unittest.skipUnless(os.geteuid() == 0 and (BIN / "logd").exists(), "requires root and host logd")
class LoggingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leon-log-test-")
        self.path = Path(self.temp.name)
        self.path.chmod(0o755)
        self.runtime = self.path / "run"
        self.journal = self.path / "journal"
        self.output = open(self.path / "daemon.log", "wb")
        self.proc = None
        self.auth = {"ok": True, "locked": False, "epoch": "a" * 32, "generation": 1}
        self.auth_stop = threading.Event()
        self.auth_socket = socket.socket(socket.AF_UNIX)
        self.auth_socket.bind(str(self.path / "auth.sock"))
        os.chmod(self.path / "auth.sock", 0o666)
        self.auth_socket.listen(16)
        self.auth_socket.settimeout(0.1)
        self.auth_thread = threading.Thread(target=self.serve_auth, daemon=True)
        self.auth_thread.start()
        self.start()

    def tearDown(self):
        self.stop()
        self.auth_stop.set()
        self.auth_thread.join(timeout=2)
        self.auth_socket.close()
        self.output.close()
        self.temp.cleanup()

    def serve_auth(self):
        while not self.auth_stop.is_set():
            try:
                client, _ = self.auth_socket.accept()
            except socket.timeout:
                continue
            with client:
                client.settimeout(1)
                try:
                    client.recv(4096)
                    client.sendall(json.dumps(self.auth).encode() + b"\n")
                except OSError:
                    pass

    def start(self, boot=BOOT_A, test=True):
        args = [BIN / "logd", "--runtime", self.runtime, "--state", self.journal,
                "--auth", self.path / "auth.sock"]
        if test:
            args += ["--test", "--boot-id", boot, "--kmsg", ""]
        self.proc = subprocess.Popen(args, stdout=self.output, stderr=self.output,
                                     start_new_session=True)
        self.wait(lambda: (self.runtime / "control.sock").exists() and self.query(op="status")["ok"])

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait(timeout=2)
            self.proc = None

    def wait(self, action, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result = action()
                if result:
                    return result
            except (OSError, ValueError, KeyError):
                pass
            time.sleep(0.02)
        self.fail("condition timed out; daemon output: " + (self.path / "daemon.log").read_text())

    def query(self, **request):
        request.setdefault("op", "query")
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(4)
            client.connect(str(self.runtime / "control.sock"))
            client.sendall(json.dumps(request).encode() + b"\n")
            data = b""
            while not data.endswith(b"\n"):
                part = client.recv(65536)
                if not part:
                    raise ValueError("incomplete response")
                data += part
            return json.loads(data)

    def send(self, data, trusted=True):
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
            client.settimeout(2)
            client.sendto(data, str(self.runtime / ("capture.sock" if trusted else "write.sock")))

    def flush(self, seq):
        self.wait(lambda: self.query(op="status").get("writer_seq", 0) >= seq)

    def test_credentials_visibility_and_control_escaping(self):
        self.send(packet("system-secret", uid=12000, scope=1, source="system"))
        self.send(packet("app-own", uid=12000))
        self.send(packet("app-other", uid=12001))
        def forged():
            self.send(packet(b"\x1b[2J\x00\x80\xe2\x80\xae" + "中文".encode(), uid=0, pid=1,
                             scope=1, source="system"), trusted=False)
            return self.query()
        own = as_uid(12000, forged)
        self.wait(lambda: len(self.query()["records"]) == 4)
        own = as_uid(12000, lambda: self.query())
        self.assertEqual(own["access"], "own")
        self.assertEqual(len(own["records"]), 2)
        forged = next(r for r in own["records"] if r["scope"] == "user")
        self.assertEqual(forged["uid"], 12000)
        self.assertNotEqual(forged["pid"], 1)
        self.assertEqual(forged["source"], "uid:12000/system")
        self.assertIn("\\x1b[2J\\x00\\x80\\xe2\\x80\\xae中文", forged["message"])
        self.assertEqual(as_uid(1000, lambda: self.query())["records"], [])
        self.assertEqual(as_uid(12000, lambda: self.query(uid=12001))["records"], [])
        self.assertFalse(as_uid(12000, lambda: self.query(op="crashes"))["ok"])
        self.assertNotIn("accepted", as_uid(12000, lambda: self.query(op="status")))

    def test_settings_lock_and_production_identity(self):
        self.send(packet("system-only", scope=1))
        self.wait(lambda: self.query()["records"])
        self.assertEqual(as_uid(11000, lambda: self.query())["access"], "system")
        self.auth["locked"] = True
        locked = as_uid(11000, lambda: self.query())
        self.assertEqual(locked["access"], "own")
        self.assertEqual(locked["records"], [])
        self.auth["locked"] = False
        self.stop()
        self.start(test=False)
        self.assertEqual(as_uid(11000, lambda: self.query())["access"], "own")

    def test_pagination_persistence_previous_boot_and_torn_tail(self):
        for i in range(12):
            self.send(packet(f"line-{i}", scope=1, level=i % 8))
        records = self.wait(lambda: (r if len(r := self.query()["records"]) == 12 else None))
        page = self.query(limit=4)
        self.assertTrue(page["more"])
        after = self.query(limit=4, after=page["next"])
        self.assertEqual([r["message"] for r in after["records"]], [f"line-{i}" for i in range(4, 8)])
        self.assertEqual([r["message"] for r in self.query(limit=2, tail=True)["records"]], ["line-10", "line-11"])
        self.assertTrue(all(r["level"] <= 3 for r in self.query(max_level=3)["records"]))
        self.assertFalse(self.query(limit=2**32 + 1)["ok"])
        self.flush(records[-1]["seq"])
        self.stop()
        with (self.journal / "journal.0").open("ab") as stream:
            stream.write(b'{"incomplete":')
        self.start()
        self.assertEqual(len(self.query()["records"]), 12)
        self.send(packet("after-restart", scope=1))
        self.wait(lambda: any(x["message"] == "after-restart" for x in self.query()["records"]))
        last = self.query()["records"]
        self.flush(last[-1]["seq"])
        self.stop()
        self.start(boot=BOOT_B)
        self.send(packet("new-boot", scope=1))
        self.wait(lambda: self.query()["records"])
        previous = self.query(boot="previous")
        self.assertEqual(previous["selected_boot"], BOOT_A)
        self.assertEqual(len(previous["records"]), 13)
        self.assertEqual([r["message"] for r in self.query()["records"]], ["new-boot"])

    def test_writer_stall_crash_and_storage_error_are_isolated(self):
        self.wait(lambda: self.query(op="status")["storage_healthy"])
        children = Path(f"/proc/{self.proc.pid}/task/{self.proc.pid}/children")
        self.wait(lambda: len(children.read_text().split()) == 1)
        writer = int(children.read_text().split()[0])
        os.kill(writer, signal.SIGSTOP)
        for i in range(400):
            self.send(packet(f"while-stalled-{i}" + "x" * 900, scope=1))
        status = self.wait(lambda: (r if (r := self.query(op="status"))["disk_queue_dropped"] > 0 else None))
        self.assertGreater(status["accepted"], 0)
        started = time.monotonic()
        self.assertTrue(self.query(op="status")["ok"])
        self.assertLess(time.monotonic() - started, 1)
        time.sleep(2.1)
        self.assertFalse(self.query(op="status")["storage_healthy"])
        os.kill(writer, signal.SIGKILL)
        self.wait(lambda: self.query(op="status")["storage_healthy"])
        self.stop()
        for path in self.journal.iterdir():
            path.unlink()
        (self.journal / "journal.0").symlink_to("/dev/full")
        self.start()
        self.send(packet("memory-survives", scope=1))
        self.wait(lambda: self.query()["records"])
        self.assertFalse(self.query(op="status")["storage_healthy"])
        self.assertTrue((self.journal / "journal.0").is_symlink())
        (self.journal / "journal.0").unlink()
        self.wait(lambda: self.query(op="status")["storage_healthy"])
        self.send(packet("disk-recovered", scope=1))
        self.wait(lambda: any(r["message"] == "disk-recovered" for r in self.query()["records"]))

    def test_rotation_malformed_fd_injection_and_rate_limit(self):
        self.send(b"malformed")
        before = len(list(Path(f"/proc/{self.proc.pid}/fd").iterdir()))
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client, open("/dev/null", "rb") as stream:
            rights = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [stream.fileno()]))]
            for _ in range(30):
                client.sendmsg([packet("injected")], rights, 0, str(self.runtime / "write.sock"))
        self.wait(lambda: self.query(op="status")["invalid"] >= 31)
        self.assertLessEqual(len(list(Path(f"/proc/{self.proc.pid}/fd").iterdir())), before + 2)
        def flood():
            for i in range(400):
                self.send(packet(str(i)), trusted=False)
            return True
        as_uid(12000, flood)
        self.wait(lambda: self.query(op="status")["rate_dropped"] > 0)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
            client.settimeout(4)
            for i in range(4000):
                client.sendto(packet(str(i) + "x" * 1000, scope=1), str(self.runtime / "capture.sock"))
                if i % 8 == 0:
                    time.sleep(0.01)
        self.wait(lambda: len(list(self.journal.glob("journal.*"))) == 4, timeout=8)
        files = list(self.journal.glob("journal.*"))
        self.assertLessEqual(sum(p.stat().st_size for p in files), 4 * 1024 * 1024)
        for path in files:
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        response = self.query(limit=128, tail=True)
        self.assertEqual(len(response["records"]), 128)
        self.assertTrue(response["tail_limited"])

    def test_cli_write_query_follow_and_root_server_verification(self):
        command = [BIN / "leonlog", "--socket", self.runtime / "control.sock"]
        subprocess.run(command + ["write", "cli-first", "--ingest", self.runtime / "write.sock"], check=True)
        self.wait(lambda: self.query()["records"])
        result = subprocess.check_output(command + ["--json"])
        self.assertEqual(json.loads(result)["records"][0]["message"], "cli-first")
        follower = subprocess.Popen(command + ["--follow"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(0.2)
            self.send(packet("cli-live", scope=1))
            time.sleep(0.4)
            follower.terminate()
            stdout, stderr = follower.communicate(timeout=4)
            self.assertIn(b"cli-first", stdout)
            self.assertIn(b"cli-live", stdout)
        finally:
            if follower.poll() is None:
                follower.kill()
                follower.communicate()
        self.assertNotIn(b"\x1b", stdout)
        self.assertEqual(stderr, b"")

    def test_auth_revocation_during_crash_query_and_singleton(self):
        # A second daemon must not unlink the first daemon's sockets.
        duplicate = subprocess.run([BIN / "logd", "--test", "--runtime", self.runtime,
                                    "--state", self.journal], capture_output=True, timeout=3)
        self.assertNotEqual(duplicate.returncode, 0)
        self.assertTrue(self.query(op="status")["ok"])
        self.stop()
        endpoint = self.path / "crash.sock"
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(str(endpoint))
            server.listen(2)
            def revoke():
                connection, _ = server.accept()
                with connection:
                    connection.recv(4096)
                    self.auth["generation"] += 1
                    connection.sendall(b'{"ok":true,"reports":[{"secret":"hidden"}],"exits":[]}\n')
            worker = threading.Thread(target=revoke, daemon=True)
            worker.start()
            self.proc = subprocess.Popen([BIN / "logd", "--test", "--runtime", self.runtime,
                "--state", self.journal, "--auth", self.path / "auth.sock", "--crash", endpoint],
                stdout=self.output, stderr=self.output, start_new_session=True)
            self.wait(lambda: self.query(op="status")["ok"])
            response = as_uid(11000, lambda: self.query(op="crashes"))
            self.assertFalse(response["ok"])
            self.assertNotIn("reports", response)
            worker.join(timeout=2)

    def test_cli_rejects_nonroot_server(self):
        directory = self.path / "untrusted"
        directory.mkdir()
        directory.chmod(0o777)
        endpoint = directory / "impostor.sock"
        child = os.fork()
        if not child:
            os.setgroups([])
            os.setgid(12000)
            os.setuid(12000)
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(endpoint))
                server.listen(2)
                connection, _ = server.accept()
                connection.close()
            os._exit(0)
        try:
            self.wait(lambda: endpoint.exists())
            result = subprocess.run([BIN / "leonlog", "--socket", endpoint, "--json"],
                                    capture_output=True, timeout=4)
            self.assertNotEqual(result.returncode, 0)
        finally:
            os.kill(child, signal.SIGKILL)
            os.waitpid(child, 0)


if __name__ == "__main__":
    unittest.main()
