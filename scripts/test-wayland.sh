#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "$0")/.." && pwd)
runtime=$(mktemp -d)
weston_pid=
cleanup() {
    if [ -n "$weston_pid" ]; then kill "$weston_pid" 2>/dev/null || true; wait "$weston_pid" 2>/dev/null || true; fi
    rm -rf "$runtime"
}
trap cleanup EXIT
chmod 0700 "$runtime"
export XDG_RUNTIME_DIR="$runtime"
export WAYLAND_DISPLAY=leon-test
mkdir -p "$repo/build/screenshots"
weston --backend=headless-backend.so --use-pixman --socket=leon-test --idle-time=0 \
    --width=480 --height=960 --shell=kiosk-shell.so --log="$runtime/weston.log" &
weston_pid=$!
for attempt in $(seq 1 100); do
    [ -S "$runtime/leon-test" ] && break
    kill -0 "$weston_pid" 2>/dev/null || { cat "$runtime/weston.log"; exit 1; }
    sleep 0.05
done
for application in settings calculator gallery files editor terminal images clock; do
    HOME="$runtime/$application" LMUI_CAPTURE_FRAME="$repo/build/screenshots/$application.png" \
        LMUI_QUIT_AFTER_FRAME=1 timeout 15 "${LEON_TEST_BIN:-$repo/build/host}/bin/leon-$application"
    test -s "$repo/build/screenshots/$application.png"
done
LMUI_CAPTURE_FRAME="$repo/build/screenshots/lock.png" LMUI_QUIT_AFTER_FRAME=1 \
    timeout 15 "${LEON_TEST_BIN:-$repo/build/host}/bin/leon-shell"
echo 'Wayland smoke: all eight apps and shell rendered successfully'
