#!/bin/bash
# usage (as root in container): ab.sh <runs> <step> [frames]
# builds base + new trees, copies binaries to /opt/nb, alternates runs.
export GI_TYPELIB_PATH=/usr/local/lib/x86_64-linux-gnu/girepository-1.0 XDG_DATA_DIRS=/usr/local/share:/usr/share
for t in base new; do
  d=/work/nautilus; [ $t = base ] && d=/work/nautilus-base
  ninja -C $d/build src/nautilus 2>&1 | grep -E -A5 "FAILED|error:" | head -20
  install -D -m755 $d/build/src/nautilus /opt/nb/$t/nautilus
done
RUNS=${1:-3}; STEP=${2:-3000}; FR=${3:-400}
for i in $(seq $RUNS); do
  for t in base new; do
    echo "== $t"
    su bench -c "NAUTILUS_BIN=/opt/nb/$t/nautilus benchrun $STEP $FR $BENCH_ENV" | grep -E "update|layout  |fps"
  done
done
