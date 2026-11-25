#!/bin/bash
# usage (as bench user): cmp.sh <rounds> <warm|cold> name=/opt/nb/x/nautilus ...
ROUNDS=$1; MODE=$2; shift 2
if [ "$MODE" = cold ]; then
  export BENCH_DIR=$HOME/cold; EXTRA="BENCH_ONEWAY=1 BENCH_WARMUP=5 BENCH_DELAY=8000"; FRAMES=1000
else
  export BENCH_DIR=$HOME/pngs; EXTRA=""; FRAMES=400
fi
for r in $(seq $ROUNDS); do
  for spec in "$@"; do
    name=${spec%%=*}; bin=${spec#*=}
    out=$(NAUTILUS_BIN=$bin benchrun 3000 $FRAMES BENCH_FIXED=name $EXTRA)
    up=$(echo "$out" | awk '/BENCH update/ {print $4}')
    lay=$(echo "$out" | awk '/BENCH layout  mean/ {print $4}')
    pnt=$(echo "$out" | awk '/BENCH paint/ {print $4}')
    fps=$(echo "$out" | awk '/BENCH fps/ {print $3}')
    echo "RESULT $MODE $name round=$r update=$up layout=$lay paint=$pnt fps=$fps"
  done
done
