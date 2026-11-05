#!/usr/bin/env python3
"""Create a deterministic .lmpkg from a manifest and application payload."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import stat
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    files = {"manifest.json": (json.dumps(manifest, ensure_ascii=False, sort_keys=True,
                                         separators=(",", ":")) + "\n").encode()}
    for path in sorted(args.payload.rglob("*")):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            parser.error(f"Only regular files and directories are allowed: {path}")
        if path.is_file():
            name = path.relative_to(args.payload).as_posix()
            if name in ("manifest.json", "checksums.json", ".installation"):
                parser.error(f"Reserved payload filename: {name}")
            files[name] = path.read_bytes()
    if manifest.get("entry") not in files:
        parser.error("Manifest executable is absent from the payload")
    files["checksums.json"] = json.dumps(
        {name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
        sort_keys=True, separators=(",", ":")).encode()
    # Gzip mtime and original filename are normalized as well as tar metadata.
    import gzip
    with args.output.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                for name, data in sorted(files.items()):
                    entry = tarfile.TarInfo(name)
                    entry.size = len(data)
                    entry.mode = 0o755 if name in (manifest["entry"], manifest.get("root", {}).get("entry")) else 0o644
                    entry.uid = entry.gid = entry.mtime = 0
                    archive.addfile(entry, io.BytesIO(data))


if __name__ == "__main__":
    main()
