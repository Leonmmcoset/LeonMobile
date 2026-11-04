#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$repo/output/host/bin/aarch64-linux-gcc"
if [ ! -x "$compiler" ]; then
    echo 'Build the reference image with make image before compiling target probes.' >&2
    exit 1
fi
mkdir -p "$repo/build"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    "$repo/tests/target/notifications.c" -o "$repo/build/notifications-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    "$repo/tests/target/system.c" "$repo/tests/target/system_transport.c" \
    "$repo/tests/target/system_sandbox.c" "$repo/src/services/appd/sandbox.c" \
    -o "$repo/build/system-probe"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -UNDEBUG -Os \
    -I"$repo/src/apps/terminal" \
    "$repo/tests/terminal.cpp" "$repo/src/apps/terminal/screen.cpp" \
    "$repo/src/apps/terminal/session.cpp" "$repo/src/apps/terminal/input.cpp" \
    -lvterm -lxkbcommon -lutil -o "$repo/build/terminal-probe"
echo 'AArch64 notification, system and terminal probes compiled under build/; no image was changed.'
