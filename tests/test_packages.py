"""Exercise installer transactions against isolated filesystem roots."""
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("LEON_TEST_BIN", REPO / "build/host"))


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.root = self.path / "root"

    def command(self, *args, success=True):
        result = subprocess.run([BIN / "bin/lmpkg", "--root", self.root, *args],
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def package(self, version="1.0.0", extra=None, arch="aarch64", corrupt=False,
                link=None, truncated=False, declarations=None):
        manifest = dict(format=1, id="org.example.demo", version=version,
                        arch=arch, lmui=1, name="Demo", entry="bin/demo")
        if declarations:
            manifest.update(declarations)
        # Installer must validate ELF identity before publishing a transaction.
        elf = bytearray(64)
        elf[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHI", elf, 16, 2, 183, 1)
        files = {"manifest.json": json.dumps(manifest).encode(), "bin/demo": bytes(elf)}
        if extra:
            files.update(extra)
        checksums = {n: hashlib.sha256(data).hexdigest() for n, data in files.items()}
        if corrupt:
            files["bin/demo"] += b"modified"
        files["checksums.json"] = json.dumps(checksums).encode()
        path = self.path / (version + str(len(list(self.path.iterdir()))) + ".lmpkg")
        with tarfile.open(path, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
            for name, data in files.items():
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
            if link:
                entry = tarfile.TarInfo(link[0])
                entry.type = tarfile.SYMTYPE
                entry.linkname = link[1]
                archive.addfile(entry)
        if truncated:
            path.write_bytes(path.read_bytes()[:45])
        return path

    def test_install_upgrade_uninstall_keep_and_purge_data(self):
        self.command("install", self.package())
        initial = json.loads(self.command("list").stdout)[0]
        self.assertEqual(initial["version"], "1.0.0")
        self.command("install", self.package("1.0.1"))
        upgraded = json.loads(self.command("list").stdout)[0]
        self.assertEqual(upgraded["uid"], initial["uid"])
        self.assertEqual(upgraded["version"], "1.0.1")
        data = self.root / "data/org.example.demo"
        data.mkdir(parents=True)
        (data / "note").write_text("persistent")
        self.command("remove", "org.example.demo")
        self.assertTrue((data / "note").exists())
        self.command("install", self.package("1.0.2"))
        self.assertEqual(json.loads(self.command("list").stdout)[0]["uid"], initial["uid"])
        self.command("remove", "org.example.demo", "--purge-data")
        self.assertFalse(data.exists())

    def test_installation_identity_changes_on_update_and_same_version_reinstall(self):
        package = self.package()
        self.command("install", package)
        marker = self.root / "apps/org.example.demo/current/.installation"
        first = marker.read_text()
        self.assertRegex(first, r"^[a-f0-9]{32}$")
        self.command("install", self.package("1.0.1"))
        second = marker.read_text()
        self.assertNotEqual(first, second)
        self.command("remove", "org.example.demo")
        self.command("install", package)
        self.assertNotIn(marker.read_text(), (first, second))
        self.command("remove", "org.example.demo")
        self.command("install", self.package(extra={".installation": b"a" * 32}), success=False)

    def test_root_helper_declaration_and_elf_are_validated_before_publish(self):
        declaration = {"permissions": ["root.exec"],
                       "root": {"entry": "libexec/admin", "description": "Inspect system"}}
        elf = bytearray(64)
        elf[:7] = b"\x7fELF\x02\x01\x01"
        struct.pack_into("<HHI", elf, 16, 2, 183, 1)
        self.command("install", self.package(declarations=declaration,
                                              extra={"libexec/admin": bytes(elf)}))
        helper = self.root / "apps/org.example.demo/current/libexec/admin"
        self.assertEqual(helper.stat().st_mode & 0o777, 0o755)
        self.command("remove", "org.example.demo")
        for fields, payload in [
            ({"permissions": ["root.exec"]}, {}),
            ({"root": declaration["root"]}, {}),
            (declaration, {}),
            (declaration, {"libexec/admin": b"#!/bin/sh"}),
            ({**declaration, "root": {"entry": "bin/demo", "description": "Unsafe"}}, {}),
            ({**declaration, "root": {"entry": "../escape", "description": "Unsafe"}}, {}),
            ({**declaration, "root": {"entry": "libexec/admin", "description": "bad\ntext"}}, {}),
            ({**declaration, "permissions": ["root.exec", "root.exec"]}, {}),
        ]:
            self.command("install", self.package(declarations=fields, extra=payload), success=False)

    def test_invalid_upgrade_keeps_current_version(self):
        self.command("install", self.package())
        current = self.root / "apps/org.example.demo/current"
        original = current.resolve()
        self.command("install", self.package("1.0.1", corrupt=True), success=False)
        self.assertEqual(current.resolve(), original)
        self.assertEqual(json.loads(self.command("list").stdout)[0]["version"], "1.0.0")

    def test_path_traversal_and_absolute_paths_rejected(self):
        for name in ("../escaped", "/tmp/leon-escaped", "bin/../../escaped", "a\\b"):
            with self.subTest(name=name):
                self.command("install", self.package(extra={name: b"bad"}), success=False)
        self.assertFalse((self.path / "escaped").exists())

    def test_links_and_wrong_architecture_rejected(self):
        self.command("install", self.package(link=("escape", "/etc")), success=False)
        self.command("install", self.package(arch="x86_64"), success=False)

    def test_truncated_archive_and_downgrade_rejected(self):
        self.command("install", self.package(truncated=True), success=False)
        self.command("install", self.package("1.1.0"))
        self.command("install", self.package("1.0.0"), success=False)

    def test_interrupted_staging_is_collected(self):
        stale = self.root / "staging/old-transaction"
        stale.mkdir(parents=True)
        (stale / "partial").write_text("partial")
        self.command("install", self.package())
        self.assertFalse(stale.exists())


if __name__ == "__main__":
    unittest.main()
