#!/bin/bash
# usage: run.sh <px-per-frame> [frames] [extra env...]
export XDG_DATA_DIRS=/usr/local/share:/usr/share
export LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu
export XDG_RUNTIME_DIR=/tmp/xdg-$(id -u); mkdir -p $XDG_RUNTIME_DIR; chmod 700 $XDG_RUNTIME_DIR
export ${BENCH_A11Y:-A11Y_ON=1}
if ! pgrep -u $(id -u) -x weston >/dev/null; then rm -f $XDG_RUNTIME_DIR/wl-bench*;
  weston --backend=headless --renderer=${WRENDERER:-pixman} --width=1600 --height=1000 --socket=wl-bench --idle-time=0 >$XDG_RUNTIME_DIR/weston.log 2>&1 &
  sleep 2
fi
export WAYLAND_DISPLAY=wl-bench GDK_BACKEND=wayland
export NAUTILUS_SCROLL_BENCH=$1 NAUTILUS_SCROLL_BENCH_FRAMES=${2:-600}
shift 2
pkill -u $(id -u) -x nautilus 2>/dev/null; sleep 0.3
export NAUTILUS_BIN BENCH_TREE; env "$@" dbus-run-session 2>&1 -- bash -c 'gsettings set org.gnome.nautilus.preferences default-folder-viewer list-view; gsettings set org.gnome.nautilus.list-view default-zoom-level small; gsettings set org.gnome.nautilus.list-view use-tree-view ${BENCH_TREE:-false}; timeout 120 ${NAUTILUS_BIN:-/usr/local/bin/nautilus} --new-window ${BENCH_DIR:-$HOME/pngs} 2>&1' | grep -E "^BENCH|CRITICAL"
