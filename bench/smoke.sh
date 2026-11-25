#!/bin/bash
# usage: smoke.sh <binary> <viewer: list-view|icon-view> [expand-tree true|false] [dir]
export XDG_DATA_DIRS=/usr/local/share:/usr/share LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu
export XDG_RUNTIME_DIR=/tmp/xdg-$(id -u) WAYLAND_DISPLAY=wl-bench GDK_BACKEND=wayland
export NAUTILUS_SCROLL_BENCH=600 NAUTILUS_SCROLL_BENCH_FRAMES=300
BIN=$1 VIEW=$2 TREE=${3:-false} DIR=${4:-$HOME/testdir}
pkill -u $(id -u) -x nautilus 2>/dev/null; sleep 0.3
export BIN VIEW TREE DIR
dbus-run-session 2>&1 -- bash -c 'gsettings set org.gnome.nautilus.preferences default-folder-viewer $VIEW; gsettings set org.gnome.nautilus.list-view use-tree-view $TREE; timeout 25 $BIN --new-window $DIR 2>&1; echo exit=$?' \
  | grep -E "CRITICAL|WARNING|Gtk-|exit=|BENCH fps" | grep -vE "Running as root|GVFS|UPower|Tracker|network:|bus can|Glycin|MESA" | sort | uniq -c | head -20
