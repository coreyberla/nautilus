#!/usr/bin/bash
# Build Nautilus from /src and record a Sysprof capture of it running under a
# headless mutter. Runs inside the container; see README.md.
#
# Usage: run-profile.sh [OUTPUT.syscap] [-- extra nautilus args]
# PROFILE_SECONDS (default 30) sets how long to record before stopping.
set -euo pipefail

out=${1:-/src/_profile/nautilus.syscap}
shift || true
[ "${1:-}" = "--" ] && shift
builddir=/src/_build-profile

# debugoptimized + frame pointers so sysprof can unwind and symbolize our code.
if [ ! -d "$builddir" ]; then
    meson setup "$builddir" /src --prefix /usr --buildtype debugoptimized \
        -Dtests=none -Dc_args=-fno-omit-frame-pointer
fi
ninja -C "$builddir"
ninja -C "$builddir" install >/dev/null

# sysprofd hands out perf fds over the system bus; polkit allows uid 0.
mkdir -p /run/dbus
[ -S /run/dbus/system_bus_socket ] || dbus-daemon --system --fork
pgrep -x polkitd >/dev/null || /usr/lib/polkit-1/polkitd --no-debug &
pgrep -x sysprofd >/dev/null || /usr/libexec/sysprofd &
sleep 1

# Symbols for anything not covered by the installed -debuginfo packages.
export DEBUGINFOD_URLS=${DEBUGINFOD_URLS:-https://debuginfod.fedoraproject.org/}

mkdir -p "$(dirname "$out")"
export XDG_RUNTIME_DIR=$(mktemp -d)
exec dbus-run-session -- bash -c '
    mutter --headless --wayland --no-x11 --virtual-monitor 1920x1080 &
    mutter_pid=$!
    for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
    export WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland
    # SIGINT makes sysprof-cli stop recording and write the capture.
    timeout -s INT --preserve-status "${PROFILE_SECONDS:-30}" \
        sysprof-cli --force --gtk --session-bus "$0" -- nautilus --new-window "$@" || rc=$?
    kill $mutter_pid
    exit ${rc:-0}
' "$out" "$@"
