#!/usr/bin/env bash
set -euo pipefail
images=$1
board=$(cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$TARGET_DIR/data"
GENIMAGE_TMP="$BUILD_DIR/genimage-leon"
rm -rf "$GENIMAGE_TMP"
"$HOST_DIR/bin/genimage" --rootpath "$TARGET_DIR" --tmppath "$GENIMAGE_TMP" --inputpath "$images" --outputpath "$images" --config "$board/genimage.cfg"
