#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
version=$(cat "$repo/VERSION")
dest="$repo/dist/leonmobile-$version-qemu-aarch64"
images="$repo/output/images"
for file in Image leon-initramfs.cpio.gz rootfs.ext4 leonmobile-qemu-aarch64.img; do
    test -s "$images/$file" || { echo "Missing $file: run make image first" >&2; exit 1; }
done
"$repo/scripts/build.sh" legal-info
mkdir -p "$dest"
for file in Image leon-initramfs.cpio.gz rootfs.ext4 leonmobile-qemu-aarch64.img; do
    cp --sparse=always "$images/$file" "$dest/$file"
done
cp -r "$repo/output/legal-info" "$dest/legal-info"
cp "$repo/README.md" "$repo/LICENSE" "$dest/"
cp -r "$repo/docs" "$dest/"
"$repo/scripts/build.sh" show-info > "$repo/.cache/release-packages.raw"
python3 - "$repo" "$dest" <<'PY'
import json,pathlib,subprocess,sys
repo,dest=map(pathlib.Path,sys.argv[1:])
raw=(repo/'.cache/release-packages.raw').read_text()
(dest/'software-manifest.json').write_text(json.dumps(json.loads(raw[raw.index('{'):raw.rindex('}')+1]),indent=2)+'\n')
commit=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
(dest/'build-info.json').write_text(json.dumps({'distribution':'LeonMobile','version':(repo/'VERSION').read_text().strip(),'kernel':'7.3.1','buildroot':'2026.08.1','arch':'aarch64','commit':commit,'status':'developer-prototype'},indent=2)+'\n')
PY
(cd "$dest"; find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum > SHA256SUMS)
echo "Release assembled: $dest"
