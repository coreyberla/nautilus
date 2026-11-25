#!/bin/bash
# usage (as bench user): matrix.sh <rounds> <warm|cold>
ROUNDS=${1:-3}; MODE=${2:-warm}
declare -A BIN ENVS
BIN[A]=/opt/nb/base/nautilus; ENVS[A]="BENCH_FIXED=none"
BIN[B]=/opt/nb/new/nautilus;  ENVS[B]="BENCH_FIXED=name BENCH_NOCACHE=1"
BIN[C]=/opt/nb/new/nautilus;  ENVS[C]="BENCH_FIXED=name"
if [ "$MODE" = cold ]; then
  export BENCH_DIR=$HOME/cold; EXTRA="BENCH_ONEWAY=1 BENCH_WARMUP=5 BENCH_DELAY=8000"; FRAMES=1000
else
  export BENCH_DIR=$HOME/pngs; EXTRA=""; FRAMES=400
fi
for r in $(seq $ROUNDS); do
  for c in A B C; do
    out=$(NAUTILUS_BIN=${BIN[$c]} benchrun 3000 $FRAMES ${ENVS[$c]} $EXTRA)
    up=$(echo "$out" | awk '/BENCH update/ {print $4}')
    lay=$(echo "$out" | awk '/BENCH layout  mean/ {print $4}')
    pnt=$(echo "$out" | awk '/BENCH paint/ {print $4}')
    fps=$(echo "$out" | awk '/BENCH fps/ {print $3}')
    echo "RESULT $MODE $c round=$r update=$up layout=$lay paint=$pnt fps=$fps"
  done
done
