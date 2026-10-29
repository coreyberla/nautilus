#!/usr/bin/bash
# Build Nautilus from /src and record a Sysprof capture of it running under a
# headless mutter. Runs as root inside a --privileged container; see README.md.
#
# Usage: run-profile.sh [OUTPUT.syscap] [-- extra nautilus args]
# PROFILE_SECONDS (default 30) sets how long to record before stopping.
# PROFILE_OPEN=DIR waits PROFILE_SETTLE seconds (default 8) for startup, then
# has the running instance open DIR and records PROFILE_SECONDS (default 5)
# more.
set -euo pipefail

out=$(realpath -m "${1:-/src/_profile/nautilus.syscap}")
shift || true
[ "${1:-}" = "--" ] && shift
builddir=/src/_build-profile
user=profiler

# debugoptimized + frame pointers so sysprof can unwind and symbolize our code.
if [ ! -d "$builddir" ]; then
    meson setup "$builddir" /src --prefix /usr --buildtype debugoptimized \
        -Dtests=none -Dc_args=-fno-omit-frame-pointer
fi
ninja -C "$builddir"
ninja -C "$builddir" install >/dev/null

# Nautilus refuses to run as root, so the session runs as an unprivileged
# user and asks sysprofd (root, over the system bus) for perf fds.
id "$user" >/dev/null 2>&1 || useradd -m "$user"
mkdir -p /etc/polkit-1/rules.d
cat > /etc/polkit-1/rules.d/50-sysprof.rules <<EOF
polkit.addRule(function(action, subject) {
    if (action.id == "org.gnome.sysprof3.profile" && subject.user == "$user")
        return polkit.Result.YES;
});
EOF

# Sysprof translates paths through the overlay layers of the container root
# (lowerdir/upperdir), which only exist on the host. Point those paths back
# at our own root so it can find the ELF files; under podman it also
# prefixes /run/host.
layers=$(awk '$5 == "/"' /proc/self/mountinfo | sed -n 's/.* - overlay [^ ]* //p' \
    | tr ',' '\n' | sed -n 's/^\(lowerdir\|upperdir\)=//p' | tr ':' '\n')
for layer in $layers; do
    if [ ! -e "$layer" ]; then
        mkdir -p "$(dirname "$layer")"
        ln -s / "$layer"
    fi
done
[ -e /run/host ] || ln -s / /run/host

# Sysprof looks for .build-id/ab/abcdef... rather than the standard
# .build-id/ab/cdef....debug, so add aliases for installed debug files.
for f in /usr/lib/debug/.build-id/??/*.debug; do
    [ -e "$f" ] || continue
    dir=$(dirname "$f")
    alias=$dir/$(basename "$dir")$(basename "$f" .debug)
    [ -e "$alias" ] || ln -s "$(basename "$f")" "$alias"
done

mkdir -p /run/dbus
[ -S /run/dbus/system_bus_socket ] || dbus-daemon --system --fork
if ! pgrep -x polkitd >/dev/null; then
    for p in /usr/lib/polkit-1/polkitd /usr/libexec/polkitd; do
        [ -x "$p" ] && { "$p" --no-debug & break; }
    done
fi
pgrep -x sysprofd >/dev/null || /usr/libexec/sysprofd &
sleep 1

mkdir -p "$(dirname "$out")"
chown "$user" "$(dirname "$out")"
runtime_dir=/run/user/$(id -u "$user")
mkdir -p "$runtime_dir"
chown "$user" "$runtime_dir"
chmod 700 "$runtime_dir"

# DEBUGINFOD_URLS is passed through, so symbols for anything not covered by
# the installed debug packages can be fetched. Sysprof writes no symbols at
# all if debuginfod is set but unreachable; pass -e DEBUGINFOD_URLS= then.
exec runuser -u "$user" -- env XDG_RUNTIME_DIR="$runtime_dir" \
    dbus-run-session -- bash -c '
    mutter --headless --wayland --no-x11 --virtual-monitor 1920x1080 &
    mutter_pid=$!
    for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/wayland-0" ] && break; sleep 0.2; done
    export WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland
    if [ -n "${PROFILE_OPEN:-}" ]; then
        # Let nautilus finish starting up, then have the running instance
        # open PROFILE_OPEN while still recording.
        sysprof-cli --force --gtk --use-trace-fd --session-bus "$0" -- nautilus --new-window "$@" &
        sysprof_pid=$!
        sleep "${PROFILE_SETTLE:-8}"
        nautilus "$PROFILE_OPEN"
        sleep "${PROFILE_SECONDS:-5}"
        # sysprof-cli stops recording and writes the capture once its child
        # exits.
        nautilus -q
        wait $sysprof_pid || rc=$?
    else
        # SIGINT makes sysprof-cli stop recording and write the capture.
        timeout -s INT --preserve-status "${PROFILE_SECONDS:-30}" \
            sysprof-cli --force --gtk --use-trace-fd --session-bus "$0" -- nautilus --new-window "$@" || rc=$?
    fi
    kill $mutter_pid
    exit ${rc:-0}
' "$out" "$@"
