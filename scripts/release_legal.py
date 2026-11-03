"""Collect pinned source and license material for external toolchain runtimes."""
import hashlib
import json
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parents[1]


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            digest.update(data)
    return digest.hexdigest()


def collect():
    manifest = json.loads((REPO / "distro/toolchain/legal-sources.json").read_text())
    cache = REPO / ".cache/toolchain-legal"
    for entry in manifest["files"]:
        relative = Path(entry["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Invalid toolchain material path")
        target = cache / relative
        if target.is_file() and sha256(target) == entry["sha256"]:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + ".part")
        for url in entry["urls"]:
            result = subprocess.run([
                "curl", "--fail", "--location", "--retry", "2",
                "--connect-timeout", "10", "--max-time", "180",
                "--silent", "--show-error", url, "-o", str(partial)])
            if result.returncode == 0 and sha256(partial) == entry["sha256"]:
                partial.replace(target)
                break
            partial.unlink(missing_ok=True)
        else:
            raise RuntimeError("Could not retrieve verified toolchain material: " + str(relative))
    return cache
