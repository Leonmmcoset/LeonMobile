#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
version=2026.08.1
sha=34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b
cache="$repo/.cache"
tree="$cache/buildroot-$version"
mkdir -p "$cache" "$repo/output"
if [ ! -f "$tree/Makefile" ]; then
    curl --fail --location --retry 3 "https://buildroot.org/downloads/buildroot-$version.tar.xz" -o "$cache/buildroot-$version.tar.xz"
    printf '%s  %s\n' "$sha" "$cache/buildroot-$version.tar.xz" | sha256sum --check --status
    tar -xf "$cache/buildroot-$version.tar.xz" -C "$cache"
fi
export BR2_DL_DIR="$cache/downloads"
export FORCE_UNSAFE_CONFIGURE=1
if [ ! -f "$repo/output/.config" ]; then
    make -C "$tree" O="$repo/output" BR2_EXTERNAL="$repo/distro" leon_qemu_aarch64_defconfig
fi
make -C "$tree" O="$repo/output" BR2_EXTERNAL="$repo/distro" -j"${JOBS:-4}" "${@:-all}"
