#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
toolchain="$repo/output/host/share/buildroot/toolchainfile.cmake"
test -f "$toolchain" || { echo 'Build the system first: make image' >&2; exit 1; }
cmake -S "$repo/examples/hello" -B "$repo/build/hello" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain"
cmake --build "$repo/build/hello"
mkdir -p "$repo/build/hello/payload/bin"
cp "$repo/build/hello/hello" "$repo/build/hello/payload/bin/hello"
python3 "$repo/scripts/make-package.py" "$repo/examples/hello/manifest.json" \
    "$repo/build/hello/payload" "$repo/build/hello/org.example.hello-1.0.0.lmpkg"
