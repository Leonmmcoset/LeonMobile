#!/usr/bin/env bash
set -euo pipefail
images=$1
board=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$TARGET_DIR/data"
GENIMAGE_TMP="$BUILD_DIR/genimage-leon"
rm -rf "$GENIMAGE_TMP"
# Both archives contain independent recovery tools and their target libraries.
python3 "$board/../../../scripts/make-initramfs.py" \
    --target "$TARGET_DIR" --stage "$BUILD_DIR/leon-initramfs" --images "$images" \
    --readelf "$HOST_DIR/bin/aarch64-linux-readelf" --epoch "${SOURCE_DATE_EPOCH:-0}"
python3 "$board/../../../scripts/make-boot.py" --images "$images" --board "$board"
"$HOST_DIR/bin/genimage" --rootpath "$TARGET_DIR" --tmppath "$GENIMAGE_TMP" --inputpath "$images" --outputpath "$images" --config "$board/genimage.cfg"
