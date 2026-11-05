"""Third-party Root requests: actual installed ELFs/Unix peers and separate UID 0 helpers."""
import json
import ctypes
import os
from pathlib import Path
import select
import re
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from test_system_services import rpc

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))
SECRET = "AppRoot!2026-test"


@unittest.skipUnless(os.geteuid() == 0, "Root broker fixtures require root")
class RootApplications(unittest.TestCase):
    def setUp(self):
        ctypes.CDLL(None).prctl(36, 1, 0, 0, 0)  # Reap helpers orphaned by the deliberate daemon crash.
        self.temp = tempfile.TemporaryDirectory(prefix="leon-root-apps-", dir="/opt")
        self.path = Path(self.temp.name)
        self.path.chmod(0o755)
        self.root = self.path / "packages"
        self.state = self.path / "state"
        self.endpoint = self.path / "root/root.sock"
        self.auth_socket = self.path / "auth/auth.sock"
        for name in ("proc", "modules", "builtin"):
            (self.path / name).mkdir()
        self.root.mkdir()
        (self.root / "installer.lock").touch(mode=0o600)
        self.children = []
        self.process = None
        self.auth = subprocess.Popen([BUILD / "bin/authd", "--test", "--state-dir",
                                      self.path / "auth-state", "--socket", self.auth_socket],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.wait_socket(self.auth_socket, self.auth)
        self.unlock()
        self.install(12000)
        self.start()
        self.assertTrue(self.call("enable", password=SECRET)["ok"])

    def install(self, uid, instance=None):
        app_id = "org.example.root" + str(uid)
        folder = self.root / "apps" / app_id / "versions/1.0.0"
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "bin").mkdir(exist_ok=True)
        (folder / "libexec").mkdir(exist_ok=True)
        for name in ("bin/app", "libexec/helper"):
            if (folder / name).exists():
                (folder / name).unlink()
            shutil.copy2(BUILD / "bin/root-app-fixture", folder / name)
            (folder / name).chmod(0o755)
        (folder / ".installation").write_text(instance or os.urandom(16).hex())
        (folder / "manifest.json").write_text(json.dumps(dict(
            format=1, id=app_id, version="1.0.0", arch="aarch64", lmui=1, name="Root fixture",
            entry="bin/app", permissions=["root.exec"],
            root=dict(entry="libexec/helper", description="Harmless fixture diagnostics"))))
        current = folder.parent.parent / "current"
        if not current.exists():
            current.symlink_to("versions/1.0.0")
        identities = self.root / "identities.json"
        data = json.loads(identities.read_text()) if identities.exists() else dict(next=12000, apps={})
        data["apps"][app_id] = uid
        data["next"] = max(uid + 1, data["next"])
        identities.write_text(json.dumps(data))
        for directory in (self.root, *self.root.rglob("*")):
            if directory.is_dir() and not directory.is_symlink():
                directory.chmod(0o755)
        return current

    def wait_socket(self, path, process):
        end = time.monotonic() + 5
        while time.monotonic() < end:
            if process.poll() is not None:
                self.fail(process.stderr.read().decode())
            if path.exists():
                try:
                    rpc(path, {"op": "status"})
                    return
                except (OSError, ValueError):
                    pass
            time.sleep(.02)
        self.fail("service did not start")

    def start(self):
        self.process = subprocess.Popen([BUILD / "bin/rootd", "--test", "--state-dir", self.state,
            "--socket", self.endpoint, "--proc", self.path / "proc", "--modules", self.path / "modules",
            "--root", self.root, "--builtin", self.path / "builtin", "--auth-socket", self.auth_socket],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.wait_socket(self.endpoint, self.process)

    def stop(self, crash=False):
        (self.process.kill if crash else self.process.terminate)()
        out, err = self.process.communicate(timeout=5)
        if not crash:
            self.assertEqual(self.process.returncode, 0, err.decode())
        self.assertNotIn(SECRET.encode(), out + err)
        self.process = None

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        if self.process:
            self.stop()
        if self.auth.poll() is None:
            self.auth.terminate()
        self.auth.communicate(timeout=5)
        self.temp.cleanup()

    def call(self, op, uid=0, **fields):
        return rpc(self.endpoint, dict(op=op, **fields), uid)

    def unlock(self):
        response = rpc(self.auth_socket, dict(op="authenticate", purpose="unlock", secret=""))
        self.assertTrue(response["ok"], response)
        self.assertTrue(rpc(self.auth_socket, dict(op="consume", purpose="unlock", token=response["token"]))["ok"])

    def line(self, child, timeout=8):
        if not select.select([child.stdout], [], [], timeout)[0]:
            self.fail("Root fixture response timed out")
        line = child.stdout.readline()
        self.assertTrue(line, child.stderr.read().decode() if child.poll() is not None else "EOF")
        return json.loads(line)

    def request(self, args=None, uid=12000, fields=None):
        def identity():
            os.setgroups([])
            os.setgid(uid)
            os.setuid(uid)
        entry = self.root / "apps" / ("org.example.root" + str(uid)) / "current/bin/app"
        request = fields if fields is not None else dict(op="app_request", args=args or [])
        process = subprocess.Popen([entry, self.endpoint, json.dumps(request)], preexec_fn=identity,
                                    env=dict(os.environ, LD_LIBRARY_PATH=str(self.path / "sdk-lib")), stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        self.children.append(process)
        return process, self.line(process)

    def prompt(self, token):
        end = time.monotonic() + 3
        while time.monotonic() < end:
            result = self.call("app_pending")
            if result.get("ok"):
                for item in result["requests"]:
                    if item["token"] == token:
                        return item
            time.sleep(.02)
        self.fail("Root prompt not published")

    def approve(self, token, password=SECRET):
        return self.call("app_decide", token=token, allow=True, password=password)

    def complete(self, child):
        output = bytearray()
        frames = []
        while True:
            frame = self.line(child)
            frames.append(frame)
            if "output_hex" in frame:
                output.extend(bytes.fromhex(frame["output_hex"]))
            if "client_uid" in frame:
                self.assertEqual(frame["client_uid"], 12000)
                break
        child.wait(timeout=3)
        return output, frames

    def gone(self, pid):
        end = time.monotonic() + 4
        while Path(f"/proc/{pid}").exists() and time.monotonic() < end:
            try:
                os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                pass
            time.sleep(.02)
        self.assertFalse(Path(f"/proc/{pid}").exists(), f"process {pid} survived")

    def test_exact_helper_binary_output_exit_status_and_application_stays_unprivileged(self):
        child, first = self.request(["binary"])
        self.assertTrue(first["ok"], first)
        prompt = self.prompt(first["token"])
        self.assertEqual(prompt["args"], ["binary"])
        self.assertEqual(prompt["pid"], child.pid)
        self.assertEqual(prompt["uid"], 12000)
        self.assertEqual(prompt["entry"], "libexec/helper")
        self.assertTrue(self.approve(first["token"])["ok"])
        output, frames = self.complete(child)
        self.assertIn(b"helper_uid=0 app=org.example.root12000 caller_uid=12000", output)
        self.assertIn(b"extra_fds=0 stdin_eof=1 injected_env=0", output)
        self.assertTrue(output.endswith(bytes([0, 255, 10, 27, 0])))
        self.assertEqual(frames[-2]["status"], 7)
        self.assertFalse(self.approve(first["token"])["ok"])
        self.assertNotIn(SECRET, "".join(p.read_text() for p in self.state.glob("*.json")))
        self.assertNotIn("output_hex", (self.state / "audit.json").read_text())

    def test_identity_spoofing_no_secrets_or_descriptors_and_argument_bounds(self):
        for op in ("app_request", "app_pending", "app_decide", "app_list", "app_policy", "exec"):
            self.assertFalse(self.call(op, uid=12000, args=[], password=SECRET, uid_spoof=0)["ok"])
        for fields in (dict(op="app_request", args=[], password=SECRET),
                       dict(op="app_request", args=[], executable="/bin/sh"),
                       dict(op="app_request", args=["x"] * 5),
                       dict(op="app_request", args=["line\ncommand"]),
                       dict(op="app_request", args=["a" * 65]),
                       dict(op="app_request", args=["\u202e"]),
                       dict(op="app_request", args=["a" * 64] * 3)):
            child, reply = self.request(fields=fields)
            self.assertFalse(reply["ok"], reply)
            child.wait(timeout=3)
        child, first = self.request()
        self.prompt(first["token"])
        self.assertFalse(self.call("app_decide", uid=11000, token=first["token"], allow=True, password=SECRET)["ok"])
        self.assertTrue(self.call("app_decide", token=first["token"], allow=False)["ok"])
        _, frames = self.complete(child)
        self.assertFalse(frames[-2]["ok"])

    def test_wrong_password_never_executes_and_every_command_requires_new_approval(self):
        child, first = self.request()
        self.prompt(first["token"])
        self.assertFalse(self.approve(first["token"], "wrong password")["ok"])
        self.assertFalse(self.approve(first["token"])["ok"])
        time.sleep(1.05)
        self.assertTrue(self.approve(first["token"])["ok"])
        self.complete(child)
        child, second = self.request()
        self.prompt(second["token"])
        self.assertNotEqual(first["token"], second["token"])
        self.assertFalse(self.approve(first["token"])["ok"])
        self.assertEqual(self.prompt(second["token"])["state"], "pending")
        self.assertTrue(self.call("app_decide", token=second["token"], allow=False)["ok"])
        self.complete(child)

    def test_block_persists_restart_unblock_requires_password_and_revoke_kills_helper(self):
        child, first = self.request()
        self.prompt(first["token"])
        self.assertTrue(self.call("app_decide", token=first["token"], allow=False, block=True)["ok"])
        self.complete(child)
        self.stop(); self.start()
        child, reply = self.request()
        self.assertFalse(reply["ok"])
        child.wait(timeout=3)
        self.assertTrue(self.call("app_list")["apps"][0]["blocked"])
        self.assertFalse(self.call("app_policy", id="org.example.root12000", blocked=False)["ok"])
        time.sleep(1.05)
        self.assertTrue(self.call("app_policy", id="org.example.root12000", blocked=False, password=SECRET)["ok"])
        child, first = self.request(["wait"])
        self.prompt(first["token"])
        self.assertTrue(self.approve(first["token"])["ok"])
        started = self.line(child)
        self.assertEqual(started["state"], "running")
        self.assertTrue(self.call("app_revoke", id="org.example.root12000")["ok"])
        self.gone(started["pid"])
        self.complete(child)
        self.assertTrue(self.call("app_policy", id="org.example.root12000", blocked=True, password=SECRET)["ok"])
        self.assertTrue(self.call("app_reset", id="org.example.root12000")["ok"])
        self.assertFalse(self.call("app_list")["apps"][0]["blocked"])

    def test_update_reinstall_and_caller_exit_invalidate_requests(self):
        for change in ("update", "reinstall", "caller-exit"):
            child, first = self.request()
            self.prompt(first["token"])
            if change == "caller-exit":
                child.terminate(); child.wait(timeout=3)
            else:
                current = self.root / "apps/org.example.root12000/current"
                if change == "reinstall":
                    shutil.rmtree(current.resolve())
                self.install(12000)
            time.sleep(.3)
            self.assertFalse(self.approve(first["token"])["ok"])
            if change != "caller-exit":
                self.complete(child)

    def test_lock_and_authentication_service_failure_cancel_pending_and_running(self):
        for running in (False, True):
            child, first = self.request(["wait"])
            self.prompt(first["token"])
            started = None
            if running:
                self.assertTrue(self.approve(first["token"])["ok"])
                started = self.line(child)
            self.assertTrue(rpc(self.auth_socket, dict(op="lock"))["ok"])
            if started:
                self.gone(started["pid"])
            _, frames = self.complete(child)
            self.assertFalse(any(f.get("state") == "finished" and f.get("ok") for f in frames))
            self.unlock()
        child, first = self.request()
        self.prompt(first["token"])
        self.auth.kill(); self.auth.communicate(timeout=5)
        self.complete(child)
        self.assertFalse(self.approve(first["token"])["ok"])

    def test_capacity_expiry_corrupt_policy_and_rootd_crash_fail_closed(self):
        child, first = self.request()
        self.prompt(first["token"])
        other, reply = self.request()
        self.assertFalse(reply["ok"])
        other.wait(timeout=3)
        self.install(12001)
        second, request = self.request(uid=12001)
        self.assertTrue(request["ok"])
        self.install(12002)
        third, reply = self.request(uid=12002)
        self.assertFalse(reply["ok"])
        third.wait(timeout=3)
        self.assertTrue(self.call("status")["ok"])
        records = list(self.state.glob("request-*.json"))
        for path in records:
            data = json.loads(path.read_text()); data["expires"] = 1
            path.write_text(json.dumps(data))
        self.complete(child)
        second.wait(timeout=3)
        child, first = self.request(["wait"])
        self.prompt(first["token"])
        self.assertTrue(self.approve(first["token"])["ok"])
        started = self.line(child)
        self.stop(crash=True)
        self.start()
        self.gone(started["pid"])
        self.complete(child)
        self.assertEqual(list(self.state.glob("request-*.json")), [])
        (self.state / "apps-policy.json").chmod(0o644)
        child, reply = self.request()
        self.assertFalse(reply["ok"])
        child.wait(timeout=3)

    def test_trusted_ui_abort_cancels_pending_and_already_approved_work(self):
        for running in (False, True):
            child, first = self.request(["wait"])
            self.prompt(first["token"])
            started = None
            if running:
                self.assertTrue(self.approve(first["token"])["ok"])
                started = self.line(child)
            self.assertTrue(self.call("app_abort", token=first["token"])["ok"])
            if started: self.gone(started["pid"])
            self.complete(child)
            self.assertFalse(self.approve(first["token"])["ok"])

    def test_public_sdk_preserves_binary_status_and_cancels_with_stop_token(self):
        entry = self.root / "apps/org.example.root12000/current/bin/app"
        shutil.copy2(BUILD / "bin/root-sdk-fixture", entry)
        entry.chmod(0o755)
        libraries = self.path / "sdk-lib"
        libraries.mkdir(mode=0o755)
        libraries.chmod(0o755)
        shutil.copy2(BUILD / "lib/libleon-root.so.1", libraries / "libleon-root.so.1")
        (libraries / "libleon-root.so.1").chmod(0o644)
        for mode in ("binary", "wait"):
            child, first = self.request([mode])
            self.assertEqual(first["state"], "pending")
            requests = self.call("app_pending")["requests"]
            self.assertEqual(len(requests), 1)
            token = requests[0]["token"]
            self.assertTrue(self.approve(token)["ok"])
            self.assertEqual(self.line(child)["state"], "running")
            if mode == "wait":
                time.sleep(.2)
                child.stdin.write(b"cancel\n"); child.stdin.flush()
            output, frames = self.complete(child)
            self.assertIn(b"helper_uid=0", output)
            self.assertEqual(frames[-2]["cancelled"], mode == "wait")
            if mode == "binary":
                self.assertEqual(frames[-2]["status"], 7)
                self.assertTrue(output.endswith(bytes([0, 255, 10, 27, 0])))
            else:
                self.gone(int(re.search(rb"helper_pid=(\d+)", output).group(1)))
        child, first = self.request()
        self.assertEqual(first["state"], "pending")
        child.stdin.write(b"cancel\n"); child.stdin.flush()
        self.complete(child)
        time.sleep(.2)
        self.assertEqual(self.call("app_pending")["requests"], [])

    def test_application_broker_stop_clear_and_portal_cannot_grant_root(self):
        current = self.root / "apps/org.example.root12000/current"
        manifest = json.loads((current / "manifest.json").read_text())
        manifest["args"] = [str(self.endpoint), json.dumps(dict(op="app_request", args=["wait"]))]
        (current / "manifest.json").write_text(json.dumps(manifest))
        portal_socket = self.path / "portal.sock"
        portal = subprocess.Popen([BUILD / "bin/portald", "--test", "--socket", portal_socket,
                                  "--state", self.path / "grants.json", "--root", self.root,
                                  "--builtin", self.path / "builtin"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        broker_socket = self.path / "apps.sock"
        broker = subprocess.Popen([BUILD / "bin/leon-launchd", "--root", self.root,
            "--builtin", self.path / "builtin", "--socket", broker_socket,
            "--runtime-dir", self.path / "users", "--root-socket", self.endpoint,
            "--portal-socket", portal_socket], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.wait_socket(portal_socket, portal)
            self.wait_socket(broker_socket, broker)
            self.assertFalse(rpc(portal_socket, dict(op="request", permission="root.exec"), 12000)["ok"])
            state = rpc(portal_socket, dict(op="app_permissions", app=manifest["id"]))
            self.assertEqual(state["permissions"], [dict(name="root.exec", state="system-confirmation")])
            for operation in ("app_stop", "app_clear", "app_uninstall"):
                self.assertTrue(rpc(broker_socket, dict(op="launch", id=manifest["id"]))["ok"])
                end = time.monotonic() + 3
                while time.monotonic() < end:
                    requests = self.call("app_pending").get("requests", [])
                    if requests:
                        break
                    time.sleep(.02)
                self.assertTrue(requests)
                token = requests[0]["token"]
                self.assertTrue(self.approve(token)["ok"])
                time.sleep(.15)
                self.assertTrue(rpc(broker_socket, dict(op=operation, id=manifest["id"]))["ok"])
                self.assertFalse(self.approve(token)["ok"])
                self.assertFalse(self.call("app_pending")["requests"])
        finally:
            broker.terminate(); broker.communicate(timeout=6)
            portal.terminate(); portal.communicate(timeout=5)

    def test_output_limit_and_timeout_terminate_helper(self):
        for mode in ("flood", "wait"):
            child, first = self.request([mode])
            self.prompt(first["token"])
            self.assertTrue(self.approve(first["token"])["ok"])
            started = self.line(child)
            output, frames = self.complete(child) if mode == "flood" else self.complete_slow(child)
            self.assertLessEqual(len(output), 65536)
            self.assertFalse(frames[-2]["ok"])
            self.gone(started["pid"])

    def complete_slow(self, child):
        # Output includes the helper's readiness before the 30-second wall deadline.
        output, frames = bytearray(), []
        while True:
            frame = self.line(child, 35)
            frames.append(frame)
            if "output_hex" in frame:
                output.extend(bytes.fromhex(frame["output_hex"]))
            if "client_uid" in frame:
                break
        child.wait(timeout=3)
        return output, frames
