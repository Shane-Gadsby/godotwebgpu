#!/bin/bash
# formula.sh <editor> <outdir>: micro shadows vs HDRP's formula, every renderer, several light elevations.
ed=$1; od=$2; mkdir -p "$od"; d=$(dirname "$0")
for m in forward_plus mobile gl_compatibility; do
  # Compatibility's HDR 2D viewport holds sRGB-encoded values.
  export SRGB=$([ $m = gl_compatibility ] && echo 1 || echo 0)
  for sh in 0 1; do
    for el in 90 60 40 25 10; do
      b="$od/${m}_s${sh}_e${el}"
      "$d/render.sh" "$ed" $m "${b}_off" elev=$el shadow=$sh micro=0 || exit 1
      "$d/render.sh" "$ed" $m "${b}_on" elev=$el shadow=$sh micro=1 opacity=1 || exit 1
      echo -n "$m shadow=$sh "; python3 "$d/analyze.py" "${b}_off.raw" "${b}_on.raw" $el 1
    done
    b="$od/${m}_s${sh}_e40_half"
    "$d/render.sh" "$ed" $m "${b}" elev=40 shadow=$sh micro=1 opacity=0.5 || exit 1
    echo -n "$m shadow=$sh "; python3 "$d/analyze.py" "$od/${m}_s${sh}_e40_off.raw" "${b}.raw" 40 0.5
  done
done
