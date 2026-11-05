#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
toolchain="$repo/output/host/share/buildroot/toolchainfile.cmake"
test -f "$toolchain" || { echo 'Build the system first: make image' >&2; exit 1; }
cmake -S "$repo/examples/root-demo" -B "$repo/build/root-demo" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain"
cmake --build "$repo/build/root-demo"
mkdir -p "$repo/build/root-demo/payload/bin" "$repo/build/root-demo/payload/libexec"
cp "$repo/build/root-demo/root-demo" "$repo/build/root-demo/payload/bin/root-demo"
cp "$repo/build/root-demo/root-diagnostic" "$repo/build/root-demo/payload/libexec/diagnostic"
python3 "$repo/scripts/make-package.py" "$repo/examples/root-demo/manifest.json" \
    "$repo/build/root-demo/payload" "$repo/build/root-demo/org.example.rootdemo-1.0.0.lmpkg"
