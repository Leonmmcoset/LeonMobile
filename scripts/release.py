#!/usr/bin/env python3
"""Assemble a self-contained local developer release in a fresh directory."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import uuid

from release_legal import REPO, collect, sha256


def main():
    version = (REPO / "VERSION").read_text().strip()
    images = REPO / "output/images"
    image_names = ("Image", "leon-initramfs.cpio.gz", "rootfs.ext4",
                   "leonmobile-qemu-aarch64.img")
    for name in image_names:
        if not (images / name).is_file():
            raise RuntimeError(f"Missing {name}: run make image first")
    legal = subprocess.run([REPO / "scripts/build.sh", "legal-info"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(legal.stdout, end="")
    legal.check_returncode()
    if any(line.startswith("ERROR:") for line in legal.stdout.splitlines()):
        raise RuntimeError("Buildroot reported errors while collecting license material")
    runtimes = collect()
    raw = subprocess.check_output([REPO / "scripts/build.sh", "show-info"], text=True)
    packages = json.loads(raw[raw.index("{"):raw.rindex("}") + 1])
    commit = subprocess.check_output(["git", "-C", REPO, "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", REPO, "status", "--porcelain"]))
    destination = REPO / f"dist/leonmobile-{version}-qemu-aarch64"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".assemble-", dir=destination.parent) as temp:
        stage = Path(temp) / destination.name
        stage.mkdir()
        for name in image_names:
            subprocess.run(["cp", "--sparse=always", images / name, stage / name], check=True)
        shutil.copytree(REPO / "output/legal-info", stage / "legal-info")
        supplement = stage / "legal-info/supplemental"
        supplement.mkdir()
        shutil.copy2(REPO / ".cache/buildroot-2026.08.1.tar.xz", supplement)
        runtime_manifest = REPO / "distro/toolchain/legal-sources.json"
        for entry in json.loads(runtime_manifest.read_text())["files"]:
            target = supplement / "toolchain-runtimes" / entry["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(runtimes / entry["path"], target)
        shutil.copy2(REPO / "distro/toolchain/legal-sources.json", supplement)
        toolchain = REPO / "output/host/opt/ext-toolchain"
        for name in ("README.txt", "summary.csv", "buildroot.config"):
            shutil.copy2(toolchain / name, supplement / ("toolchain-" + name))
        (supplement / "README.txt").write_text(
            "This directory supplements Buildroot's legal-info output.\n"
            "The pinned Buildroot source archive contains the build infrastructure.\n"
            "Toolchain runtime sources include musl 1.2.5, GCC 14.3.0, the Bootlin\n"
            "GCC patch series and their license notices. See legal-sources.json\n"
            "for hashes and provenance and toolchain-README.txt for reproduction.\n"
            "The complete external host toolchain is not redistributed here.\n")
        for name in ("README.md", "LICENSE"):
            shutil.copy2(REPO / name, stage)
        shutil.copytree(REPO / "docs", stage / "docs")
        (stage / "scripts").mkdir()
        for name in ("run-qemu.sh", "update-image.py"):
            shutil.copy2(REPO / "scripts" / name, stage / "scripts")
        launcher = stage / "run.sh"
        launcher.write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'release=$(cd -- "$(dirname -- "$0")" && pwd)\n'
            'export LEON_IMAGES="$release"\n'
            'export LEON_DISK=${LEON_DISK:-"$release/leonmobile-user.img"}\n'
            'exec "$release/scripts/run-qemu.sh" "$@"\n')
        launcher.chmod(0o755)
        for name in ("run-qemu.sh", "update-image.py"):
            (stage / "scripts" / name).chmod(0o755)
        (stage / "software-manifest.json").write_text(json.dumps(packages, indent=2) + "\n")
        info = {"distribution": "LeonMobile", "version": version, "kernel": "7.3.1",
                "buildroot": "2026.08.1", "arch": "aarch64", "commit": commit,
                "source_dirty": dirty, "status": "developer-prototype"}
        (stage / "build-info.json").write_text(json.dumps(info, indent=2) + "\n")
        package = REPO / "build/hello/org.example.hello-1.0.0.lmpkg"
        if package.is_file():
            (stage / "examples").mkdir()
            shutil.copy2(package, stage / "examples")
        checksums = []
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                checksums.append(f"{sha256(path)}  {path.relative_to(stage).as_posix()}\n")
        (stage / "SHA256SUMS").write_text("".join(checksums))
        if destination.exists():
            previous = destination.with_name(".previous-" + uuid.uuid4().hex[:12])
            destination.rename(previous)
            try:
                stage.rename(destination)
            except BaseException:
                previous.rename(destination)
                raise
            print("Previous local assembly preserved:", previous)
        else:
            stage.rename(destination)
    print("Release assembled:", destination)


if __name__ == "__main__":
    main()
