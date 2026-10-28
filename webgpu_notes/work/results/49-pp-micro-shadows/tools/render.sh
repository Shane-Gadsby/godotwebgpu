#!/bin/bash
# render.sh <editor> <method> <out_base> args...   (args go to micro_test.gd)
ed=$1; m=$2; out=$3; shift 3
d=$(dirname "$0")
xvfb-run -a -s "-screen 0 1400x900x24" "$ed" --path "$d/proj" --rendering-method "$m" --resolution 512x512 -s micro_test.gd -- out="$out" "$@" > "$out.log" 2>&1
grep -q "^saved" "$out.log" || { echo "RENDER FAILED: $out"; tail -5 "$out.log"; exit 1; }
