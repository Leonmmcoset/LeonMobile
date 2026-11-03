#!/usr/bin/env bash
set -euo pipefail
images=$1
board=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$TARGET_DIR/data"
GENIMAGE_TMP="$BUILD_DIR/genimage-leon"
rm -rf "$GENIMAGE_TMP"
"$HOST_DIR/bin/genimage" --rootpath "$TARGET_DIR" --tmppath "$GENIMAGE_TMP" --inputpath "$images" --outputpath "$images" --config "$board/genimage.cfg"
# Minimal early userspace; source files and mtimes are sorted for reproducibility.
early="$BUILD_DIR/leon-initramfs"
rm -rf "$early"
mkdir -p "$early"/{bin,sbin,lib,dev,proc,sys,newroot}
cp "$TARGET_DIR/sbin/leon-init" "$early/init"
cp "$TARGET_DIR/bin/busybox" "$early/bin/busybox"
cp -L "$TARGET_DIR/lib/ld-musl-aarch64.so.1" "$early/lib/ld-musl-aarch64.so.1"
ln -s ld-musl-aarch64.so.1 "$early/lib/libc.so"
# BusyBox feature selection can introduce libgcc; include it if present.
if [ -e "$TARGET_DIR/lib/libgcc_s.so.1" ]; then cp -L "$TARGET_DIR/lib/libgcc_s.so.1" "$early/lib/"; fi
chmod 0755 "$early/init" "$early/bin/busybox"
find "$early" -print0 | xargs -0 touch -h -d "@${SOURCE_DATE_EPOCH:-0}"
(cd "$early"; find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --owner=0:0 2>/dev/null | gzip -n -9) > "$images/leon-initramfs.cpio.gz"
