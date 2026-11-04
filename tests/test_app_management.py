"""Settings app management with real peer UIDs, processes and private storage."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


def rpc(endpoint, request, uid=0):
    code = ("import socket,sys; s=socket.socket(socket.AF_UNIX); s.settimeout(16); "
            "s.connect(sys.argv[1]); s.sendall(sys.argv[2].encode()+b'\\n'); "
            "s.shutdown(socket.SHUT_WR); sys.stdout.buffer.write(s.makefile('rb').read())")
    result = subprocess.run([sys.executable, "-c", code, str(endpoint), json.dumps(request)],
                            user=uid, group=uid, extra_groups=[], capture_output=True,
                            timeout=18, check=True)
    return json.loads(result.stdout)


@unittest.skipUnless(os.geteuid() == 0, "app management needs real application identities")
class AppManagementTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leon-management-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.path.chmod(0o755)
        self.root = self.path / "root"
        self.root.mkdir(mode=0o755)
        self.root.chmod(0o755)
        (self.root / "data").mkdir(mode=0o755)
        (self.root / "data").chmod(0o755)
        self.builtin = self.path / "builtin"
        self.builtin.mkdir(mode=0o755)
        self.builtin.chmod(0o755)
        self.helper = self.path / "client.py"
        self.helper.write_text(textwrap.dedent("""\
            import os, pathlib, signal, socket, time
            home = pathlib.Path(os.environ['HOME'])
            (home / 'ready').touch()
            channel = socket.socket(fileno=int(os.environ['LEON_LIFECYCLE_FD']))
            channel.setblocking(True)
            while packet := channel.recv(128):
                seq, event = packet.decode().split()
                if event == 'shutdown':
                    # A late lifecycle checkpoint must also be removed.
                    (home / 'checkpoint').write_text('last write before exit')
                channel.send((seq + ' ok').encode())
                if event == 'shutdown': break
            """))
        self.helper.chmod(0o644)
        for name in ("settings", "gallery", "calculator"):
            manifest = dict(format=1, id="org.leon." + name, name=name, name_en=name,
                            version="1.0.0", arch="aarch64", lmui=1, lifecycle=1,
                            entry=sys.executable, args=[str(self.helper)],
                            permissions=["network", "files.import"])
            (self.builtin / (name + ".json")).write_text(json.dumps(manifest))
        self.local = "org.example.demo"
        (self.root / "identities.json").write_text(json.dumps(
            dict(next=12001, apps={self.local: 12000})))
        self.install_local()
        self.portal_endpoint = self.path / "portal.sock"
        self.apps_endpoint = self.path / "apps.sock"
        self.portal = self.start_portal()
        self.broker = self.start([BUILD / "bin/leon-launchd", "--root", self.root,
                                  "--builtin", self.builtin, "--socket", self.apps_endpoint,
                                  "--runtime-dir", self.path / "runtime", "--portal-socket",
                                  self.portal_endpoint], self.apps_endpoint)

    def start(self, args, endpoint):
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def finish():
            if process.poll() is None: process.terminate()
            _, error = process.communicate(timeout=7)
            self.assertEqual(process.returncode, 0, error.decode())
        self.addCleanup(finish)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.assertIsNone(process.poll())
            if endpoint.exists():
                try:
                    rpc(endpoint, {"op": "__probe"})
                    return process
                except (OSError, subprocess.SubprocessError): pass
            time.sleep(.02)
        self.fail("service did not start")

    def start_portal(self):
        return self.start([BUILD / "bin/portald", "--test", "--root", self.root,
                           "--builtin", self.builtin, "--state", self.root / "grants.json",
                           "--socket", self.portal_endpoint], self.portal_endpoint)

    def install_local(self):
        package = self.root / "apps" / self.local
        for version in ("1.0.0", "1.0.1"):
            directory = package / "versions" / version
            directory.mkdir(parents=True)
            (directory / "bin").mkdir()
            (directory / "bin/demo").write_bytes(b"fixture " * 1024)
            manifest = dict(format=1, id=self.local, name="Demo", version=version,
                            arch="aarch64", lmui=1, entry="bin/demo", permissions=["network"])
            (directory / "manifest.json").write_text(json.dumps(manifest))
        (package / "current").symlink_to("versions/1.0.1")

    def call(self, op, id=None, uid=11000, **fields):
        return rpc(self.apps_endpoint, dict(op=op, **({"id": id} if id is not None else {}),
                                            **fields), uid)

    def info(self, id="org.leon.gallery"):
        result = self.call("app_info", id)
        self.assertTrue(result["ok"], result)
        return result["app"]

    def launch(self, id="org.leon.gallery"):
        result = self.call("launch", id, uid=1000)
        self.assertTrue(result["ok"], result)
        deadline = time.monotonic() + 4
        while not (self.root / "data" / id / "ready").exists() and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertTrue((self.root / "data" / id / "ready").exists())
        return result["pid"]

    def grant(self, permission="network", uid=11002):
        token = rpc(self.portal_endpoint, {"op": "request", "permission": permission}, uid)["token"]
        self.assertTrue(rpc(self.portal_endpoint, {"op": "decide", "token": token, "allow": True}, 1000)["ok"])

    def test_metadata_roles_and_no_side_effects_for_protected_apps(self):
        apps = self.call("app_list")["apps"]
        self.assertEqual(len(apps), 4)
        local = self.info(self.local)
        self.assertEqual(local["version"], "1.0.1")
        self.assertEqual(local["source"], "local-unsigned")
        self.assertTrue(local["actions"]["uninstall"])
        self.assertEqual(self.info()["source"], "system-image")
        self.assertEqual(self.info()["permissions"][0]["state"], "not-requested")
        self.launch()
        self.launch("org.leon.settings")
        for op in ("app_list", "app_info", "app_stop", "app_clear", "app_uninstall"):
            self.assertFalse(self.call(op, "org.leon.gallery", uid=11002)["ok"])
        for op in ("app_stop", "app_clear", "app_uninstall"):
            self.assertFalse(self.call(op, "org.leon.settings")["ok"])
        self.assertFalse(self.call("app_uninstall", "org.leon.gallery")["ok"])
        self.assertFalse(self.call("remove", "org.leon.gallery", uid=1000)["ok"])
        self.assertTrue(self.info()["running"])
        self.assertTrue(self.info("org.leon.settings")["running"])
        self.assertFalse(any(self.info("org.leon.settings")["actions"].values()))
        for bad in ("../gallery", "org.example.absent", "", 5, None):
            self.assertFalse(self.call("app_clear", bad)["ok"])
        self.assertFalse(self.call("install", uid=11000, file="/tmp/untrusted")["ok"])
        for op in ("app_permissions", "reset_app"):
            for uid in (1000, 11000, 11002):
                self.assertFalse(rpc(self.portal_endpoint, {"op": op, "app": "org.leon.gallery"}, uid)["ok"])

    def test_clear_stops_checkpoint_writer_resets_permissions_and_preserves_neighbors(self):
        self.launch()
        home = self.root / "data/org.leon.gallery"
        (home / "private").write_text("user data")
        other = self.root / "data/org.leon.calculator"
        other.mkdir(); (other / "untouched").write_text("other app")
        (self.root / "device-policy.json").write_text("device credential")
        self.grant()
        self.grant("files.import")
        token = rpc(self.portal_endpoint, {"op": "choose"}, 11002)["token"]
        self.assertEqual([p["state"] for p in self.info()["permissions"]], ["allowed", "allowed"])
        result = self.call("app_clear", "org.leon.gallery")
        self.assertTrue(result["ok"], result)
        time.sleep(.15)
        self.assertFalse(home.exists())
        self.assertFalse((self.path / "runtime/11002").exists())
        self.assertFalse(self.info()["running"])
        self.assertEqual(self.info()["storage"]["data"], dict(available=True, bytes=0))
        self.assertEqual((other / "untouched").read_text(), "other app")
        self.assertEqual((self.root / "device-policy.json").read_text(), "device credential")
        self.assertFalse(rpc(self.portal_endpoint, {"op": "selection", "token": token}, 11002)["ok"])
        self.portal.terminate(); self.portal.communicate(timeout=5)
        self.portal = self.start_portal()
        self.assertEqual([p["state"] for p in self.info()["permissions"]], ["not-requested"] * 2)
        self.launch()
        self.assertFalse((home / "private").exists())
        self.assertTrue(self.call("app_stop", "org.leon.gallery")["ok"])
        self.assertTrue((home / "checkpoint").exists())

    def test_permission_states_and_pending_invalidation(self):
        token = rpc(self.portal_endpoint, {"op": "request", "permission": "network"}, 11002)["token"]
        self.assertEqual(self.info()["permissions"][0]["state"], "pending")
        self.assertTrue(rpc(self.portal_endpoint, {"op": "decide", "token": token, "allow": False}, 1000)["ok"])
        self.assertEqual(self.info()["permissions"][0]["state"], "denied")
        token = rpc(self.portal_endpoint, {"op": "request", "permission": "network"}, 11002)["token"]
        self.assertTrue(self.call("app_clear", "org.leon.gallery")["ok"])
        self.assertFalse(rpc(self.portal_endpoint, {"op": "decide", "token": token, "allow": True}, 1000)["ok"])

    def test_uninstall_purges_versions_data_and_grants_retains_identity(self):
        home = self.root / "data" / self.local
        home.mkdir(parents=True); (home / "secret").write_text("private")
        self.grant(uid=12000)
        identities = (self.root / "identities.json").read_bytes()
        result = self.call("app_uninstall", self.local)
        self.assertTrue(result["ok"], result)
        self.assertFalse(home.exists())
        self.assertFalse((self.root / "apps" / self.local).exists())
        self.assertFalse(self.call("app_info", self.local)["ok"])
        self.assertEqual((self.root / "identities.json").read_bytes(), identities)
        self.install_local()
        self.assertEqual(self.info(self.local)["permissions"][0]["state"], "not-requested")

    def test_storage_never_follows_links_or_double_counts_hardlinks(self):
        home = self.root / "data/org.leon.gallery"
        home.mkdir(parents=True)
        outside = self.path / "outside"
        outside.mkdir(); secret = outside / "secret"
        secret.write_bytes(b"secret" * 100000)
        (home / "link").symlink_to(outside, target_is_directory=True)
        (home / "file-link").symlink_to(secret)
        os.link(secret, home / "hard-1"); os.link(secret, home / "hard-2")
        os.mkfifo(home / "pipe")
        seen, expected = set(), 0
        for file in (home, *home.iterdir()):
            st = file.lstat()
            if (st.st_dev, st.st_ino) not in seen:
                expected += st.st_blocks * 512
                seen.add((st.st_dev, st.st_ino))
        self.assertEqual(self.info()["storage"]["data"], dict(available=True, bytes=expected))
        self.assertTrue(self.call("app_clear", "org.leon.gallery")["ok"])
        self.assertEqual(secret.read_bytes(), b"secret" * 100000)
        home.symlink_to(outside, target_is_directory=True)
        self.assertTrue(self.call("app_clear", "org.leon.gallery")["ok"])
        self.assertFalse(home.is_symlink())
        self.assertTrue(secret.exists())

    def test_storage_walk_limits_and_permission_service_failure_are_explicit(self):
        home = self.root / "data/org.leon.gallery"
        deep = home.joinpath(*(["d"] * 50)); deep.mkdir(parents=True)
        info = self.info()
        self.assertFalse(info["storage"]["data"]["available"])
        self.assertNotIn("bytes", info["storage"]["data"])
        self.assertFalse(self.call("app_clear", "org.leon.gallery")["ok"])
        self.portal.terminate(); self.portal.communicate(timeout=5)
        info = self.info()
        self.assertFalse(info["permissions_available"])
        self.assertTrue(all(p["state"] == "unavailable" for p in info["permissions"]))
        marker = home / "keep"; marker.write_text("do not erase on portal failure")
        self.assertFalse(self.call("app_clear", "org.leon.gallery")["ok"])
        self.assertTrue(marker.exists())
        self.assertFalse(self.call("app_uninstall", self.local)["ok"])
        self.assertTrue((self.root / "apps" / self.local / "current").exists())


if __name__ == "__main__":
    unittest.main()
