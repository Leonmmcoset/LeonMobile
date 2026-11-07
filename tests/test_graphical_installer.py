"""Reviewed package transactions, using real socket UIDs and immutable inputs."""
import hashlib
import fcntl
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

from test_app_management import rpc
import test_packages

BIN = test_packages.BIN


@unittest.skipUnless(os.geteuid() == 0, "installer authorization needs real peer UIDs")
class GraphicalInstaller(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leon-install-ui-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.path.chmod(0o755)
        self.root = self.path / "root"
        self.home = self.root / "data/org.leon.installer"
        self.home.mkdir(parents=True)
        self.home.chmod(0o700)
        os.chown(self.home, 11008, 11008)
        self.builtin = self.root / "builtin"
        self.builtin.mkdir()
        manifest = dict(format=1, id="org.leon.installer", version="1.0.0", arch="aarch64",
                        lmui=1, name="Installer", entry="/usr/bin/leon-installer")
        (self.builtin / "installer.json").write_text(json.dumps(manifest))
        self.endpoint = self.path / "apps.sock"
        self.packager = test_packages.PackageTests()
        self.packager.path = self.path
        self.packager.root = self.root
        self.process = None
        self.start()
        self.addCleanup(self.stop)

    def start(self):
        self.process = subprocess.Popen([BIN / "bin/leon-launchd", "--root", self.root,
            "--builtin", self.builtin, "--socket", self.endpoint,
            "--runtime-dir", self.path / "runtime"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll())
            if self.endpoint.exists():
                try:
                    if self.call("package_status")["ok"]: return
                except (OSError, subprocess.SubprocessError): pass
            time.sleep(.02)
        self.fail("application broker did not start")

    def stop(self):
        if self.process is not None:
            if self.process.poll() is None: self.process.terminate()
            _, error = self.process.communicate(timeout=5)
            self.assertEqual(self.process.returncode, 0, error.decode())
            self.process = None

    def call(self, op, uid=11008, **fields):
        return rpc(self.endpoint, dict(op=op, **fields), uid)

    def incoming(self, archive):
        target = self.home / "incoming.lmpkg"
        target.unlink(missing_ok=True)
        target.write_bytes(archive.read_bytes())
        target.chmod(0o600)
        os.chown(target, 11008, 11008)

    def wait(self, token):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            result = self.call("package_status", token=token)
            self.assertTrue(result["ok"], result)
            if result["state"] not in ("checking", "installing"): return result
            # A package operation must not monopolize lifecycle/status IPC.
            self.assertTrue(rpc(self.endpoint, {"op": "list"}, 1000)["ok"])
            time.sleep(.02)
        self.fail("package worker did not finish")

    def prepare(self, archive):
        self.incoming(archive)
        response = self.call("package_prepare")
        self.assertTrue(response["ok"], response)
        result = self.wait(response["token"])
        self.assertEqual(result["state"], "ready", result)
        return result

    def commit(self, prepared):
        result = self.call("package_commit", token=prepared["token"])
        self.assertTrue(result["ok"], result)
        return self.wait(prepared["token"])

    def offline(self, *args):
        return self.packager.command(*args)

    def test_review_confirmation_roles_and_immutable_source(self):
        archive = self.packager.package(declarations={"permissions": ["network", "files.import"]})
        for uid in (1000, 11000, 11003, 12000):
            for op in ("package_prepare", "package_status", "package_commit", "package_cancel"):
                self.assertFalse(self.call(op, uid=uid, token="f" * 32)["ok"])
        self.assertFalse(self.call("package_commit")["ok"])
        prepared = self.prepare(archive)
        review = prepared["review"]
        self.assertEqual(review["package"]["version"], "1.0.0")
        self.assertEqual(review["signature"], "unsigned")
        self.assertEqual(review["sha256"], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(review["added_permissions"], ["network", "files.import"])
        self.assertIsNone(review["installed"])
        self.assertFalse((self.root / "apps/org.example.demo/current").exists())
        self.assertFalse(self.call("package_commit", token="a" * 32)["ok"])
        self.incoming(self.packager.package("9.0.0"))
        self.assertEqual(self.commit(prepared)["state"], "installed")
        installed = json.loads((self.root / "apps/org.example.demo/current/manifest.json").read_text())
        self.assertEqual(installed["version"], "1.0.0")
        self.assertEqual(installed["permissions"], review["package"]["permissions"])
        self.assertFalse(self.call("package_commit", token=prepared["token"])["ok"])

    def test_upgrade_added_permissions_retains_identity_and_data(self):
        self.offline("install", self.packager.package())
        before = json.loads(self.offline("list").stdout)
        data = self.root / "data/org.example.demo"
        data.mkdir(); (data / "note").write_text("preserve me")
        prepared = self.prepare(self.packager.package("1.1.0", declarations={"permissions": ["network"]}))
        self.assertEqual(prepared["review"]["installed"]["version"], "1.0.0")
        self.assertEqual(prepared["review"]["added_permissions"], ["network"])
        self.assertEqual(self.commit(prepared)["state"], "installed")
        after = json.loads(self.offline("list").stdout)
        self.assertEqual([a["uid"] for a in before], [a["uid"] for a in after])
        self.assertEqual((data / "note").read_text(), "preserve me")
        self.assertTrue(self.call("package_cancel")["ok"])
        downgrade = self.prepare(self.packager.package("1.0.0"))
        self.assertFalse(downgrade["review"]["installable"])
        self.assertFalse(self.call("package_commit", token=downgrade["token"])["ok"])

    def test_changed_installation_invalidates_review_even_same_version_reinstall(self):
        original = self.packager.package()
        self.offline("install", original)
        prepared = self.prepare(self.packager.package("2.0.0"))
        self.offline("remove", "org.example.demo")
        self.offline("install", original)
        result = self.commit(prepared)
        self.assertEqual(result["state"], "failed", result)
        self.assertIn("changed", result["error"])
        self.assertEqual(json.loads((self.root / "apps/org.example.demo/current/manifest.json").read_text())["version"], "1.0.0")

    def test_corruption_unsafe_files_and_cancel_never_publish(self):
        self.incoming(self.packager.package(corrupt=True))
        response = self.call("package_prepare")
        self.assertTrue(response["ok"], response)
        self.assertEqual(self.wait(response["token"])["state"], "failed")
        self.assertFalse((self.root / "apps/org.example.demo/current").exists())
        self.assertTrue(self.call("package_cancel")["ok"])
        prepared = self.prepare(self.packager.package())
        self.assertTrue(self.call("package_cancel", token=prepared["token"])["ok"])
        self.assertFalse(self.call("package_commit", token=prepared["token"])["ok"])
        target = self.home / "incoming.lmpkg"
        target.unlink(); target.symlink_to("/etc/passwd")
        self.assertFalse(self.call("package_prepare")["ok"])
        target.unlink(); os.mkfifo(target); os.chown(target, 11008, 11008)
        self.assertFalse(self.call("package_prepare")["ok"])
        target.unlink(); target.write_bytes(b"untrusted owner")
        self.assertFalse(self.call("package_prepare")["ok"])
        self.assertFalse((self.root / "apps/org.example.demo/current").exists())

    def test_restart_drops_unconfirmed_review(self):
        prepared = self.prepare(self.packager.package())
        self.stop(); self.start()
        self.assertEqual(self.call("package_status")["state"], "idle")
        self.assertFalse(self.call("package_commit", token=prepared["token"])["ok"])
        self.assertFalse((self.root / "apps/org.example.demo/current").exists())

    def test_waiting_inspector_is_cancelable_without_blocking_broker(self):
        self.incoming(self.packager.package())
        with (self.root / "installer.lock").open("a+b") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            response = self.call("package_prepare")
            self.assertTrue(response["ok"], response)
            self.assertEqual(self.call("package_status")["state"], "checking")
            self.assertTrue(rpc(self.endpoint, {"op": "list"}, 1000)["ok"])
            self.assertTrue(self.call("package_cancel", token=response["token"])["ok"])
        self.assertEqual(self.call("package_status")["state"], "idle")
        self.assertFalse((self.root / "apps/org.example.demo/current").exists())

    def test_system_app_replacement_is_reviewed_but_not_installable(self):
        prepared = self.prepare(self.packager.package(declarations={"id": "org.leon.installer"}))
        self.assertFalse(prepared["review"]["installable"])
        self.assertTrue(prepared["review"]["installed"]["builtin"])
        self.assertFalse(self.call("package_commit", token=prepared["token"])["ok"])
