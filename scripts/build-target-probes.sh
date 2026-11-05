#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$repo/output/host/bin/aarch64-linux-gcc"
if [ ! -x "$compiler" ]; then
    echo 'Build the reference image with make image before compiling target probes.' >&2
    exit 1
fi
mkdir -p "$repo/build"
"$compiler" -std=c17 -D_GNU_SOURCE -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/log-client" -c "$repo/src/libraries/log-client/client.c" \
    -o "$repo/build/log-client-probe.o"
"$compiler" -std=c17 -D_GNU_SOURCE -Wall -Wextra -Werror -Os \
    -I"$repo/src/services/common" -I"$repo/src/services/serviced" \
    "$repo/tests/target/boot_console.c" "$repo/src/services/common/boot_log.c" \
    -o "$repo/build/boot-console-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    "$repo/tests/target/notifications.c" -o "$repo/build/notifications-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/log-client" \
    "$repo/tests/target/system.c" "$repo/tests/target/system_transport.c" \
    "$repo/tests/target/system_sandbox.c" "$repo/tests/target/logging.c" \
    "$repo/src/services/appd/sandbox.c" "$repo/build/log-client-probe.o" \
    -o "$repo/build/system-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    "$repo/tests/target/applications.c" "$repo/tests/target/system_transport.c" \
    -o "$repo/build/applications-probe"
"$compiler" -std=c17 -D_GNU_SOURCE -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/root-client" -c "$repo/src/libraries/root-client/wire.c" \
    -o "$repo/build/root-wire-probe.o"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/root-client" \
    "$repo/tests/target/auth.c" "$repo/tests/target/system_transport.c" \
    "$repo/build/root-wire-probe.o" -o "$repo/build/auth-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/root-client" \
    "$repo/tests/target/root.c" "$repo/tests/target/system_transport.c" \
    "$repo/build/root-wire-probe.o" -o "$repo/build/root-probe"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -UNDEBUG -Os \
    -I"$repo/src/apps/terminal" \
    "$repo/tests/terminal.cpp" "$repo/src/apps/terminal/screen.cpp" \
    "$repo/src/apps/terminal/session.cpp" "$repo/src/apps/terminal/input.cpp" \
    -lvterm -lxkbcommon -lutil -o "$repo/build/terminal-probe"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -Os \
    -I"$repo/output/host/aarch64-buildroot-linux-musl/sysroot/usr/include/cairo" \
    "$repo/tests/wayland_spoof.cpp" -llmui -o "$repo/build/auth-forged-probe"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os \
    -I"$repo/src/libraries/root-client" "$repo/tests/target/root_app.c" \
    "$repo/build/root-wire-probe.o" -o "$repo/build/root-app-fixture"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -Os -pthread \
    -I"$repo/src/libraries/root-client" -I"$repo/src/libraries/log-client" \
    "$repo/tests/target/root_applications.cpp" \
    "$repo/build/root-wire-probe.o" "$repo/build/log-client-probe.o" -o "$repo/build/root-applications-probe"
mkdir -p "$repo/build/root-probe-package/bin" "$repo/build/root-probe-package/libexec"
cp "$repo/build/root-app-fixture" "$repo/build/root-probe-package/bin/app"
cp "$repo/build/root-app-fixture" "$repo/build/root-probe-package/libexec/helper"
python3 - "$repo/build/root-probe-manifest.json" <<'PY'
import json, sys
from pathlib import Path
Path(sys.argv[1]).write_text(json.dumps(dict(format=1, id="org.example.rootprobe", version="1.0.0",
    arch="aarch64", lmui=1, name="Root target fixture", entry="bin/app",
    args=["/run/leon-root/control.sock", "@home"], permissions=["root.exec"],
    root=dict(entry="libexec/helper", description="Harmless target cgroup and credential diagnostics"))))
PY
python3 "$repo/scripts/make-package.py" "$repo/build/root-probe-manifest.json" \
    "$repo/build/root-probe-package" "$repo/build/root-app-fixture-package.lmpkg"
"$compiler" -std=c17 -Wall -Wextra -Werror -Os -c \
    "$repo/tests/target/system_transport.c" -o "$repo/build/platform-transport.o"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -Os \
    "$repo/tests/target/platform.cpp" "$repo/build/platform-transport.o" \
    -lleon-platform -o "$repo/build/platform-probe"
"${compiler%gcc}g++" -std=c++20 -Wall -Wextra -Werror -Wno-missing-field-initializers -Os \
    "$repo/tests/target/preferences.cpp" "$repo/build/platform-transport.o" \
    -o "$repo/build/preferences-probe"
echo 'AArch64 boot console, platform services, preferences, application management, authentication, notification, system, root, third-party root and terminal probes compiled under build/; no image was changed.'
