#!/bin/bash
# cost.sh <editor> <outfile>: GPU time of the full-screen test quad at 2560x1440, micro shadows off/on, alternating.
ed=$1; out=$2; d=$(cd "$(dirname "$0")"; pwd); : > "$out"
for rep in 1 2 3; do
  for m in forward_plus mobile gl_compatibility; do
    for mic in 0 1; do
      r=$(xvfb-run -a -s "-screen 0 2700x1600x24" "$ed" --path "$d/proj" --rendering-method $m --resolution 2560x1440 -s micro_test.gd -- cost=1 elev=40 shadow=1 micro=$mic 2>&1 | grep "saved cost")
      echo "$m micro=$mic rep=$rep $r" | tee -a "$out"
    done
  done
done
