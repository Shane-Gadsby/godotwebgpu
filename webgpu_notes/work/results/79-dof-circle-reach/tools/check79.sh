#!/bin/bash
# check79.sh <outdir>: the task 79 checks, pre-change (base) editor against the new one, Forward+ and Mobile.
# Long lens case (200 mm f/2.8, focus 5 m, wall 50 m, 2560x1440): model radius 80.4 px; 3% = 43.2 px, 6% = 86.4 px.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w79
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-79/bin/godot.linuxbsd.editor.x86_64
OLD=$S/godot_pre79
out=$1; mkdir -p $out
LONG="phys=1 f=200 n=2.8 focus=5 dist=50"
declare -A cases=(
 [circ2_long]="$LONG shape=circle quality=2"            # clamps at 3% now (6% before)
 [ap6_long]="$LONG shape=hexagon quality=2 aperture=6"  # custom aperture: circular gather, clamps at 3% now
 [circ2_long_max2]="$LONG shape=circle quality=2 maxblur=2"  # a setting below 3% still lowers the circle
 [circ2_long_max3]="$LONG shape=circle quality=2 maxblur=3"  # identical expected
 [hex2_long]="$LONG shape=hexagon quality=2"            # Box/Hexagon keep 6%: identical expected
 [box2_long]="$LONG shape=box quality=2"
 [hex0_long]="$LONG shape=hexagon quality=0"
 [circ2_legacy_long]="$LONG shape=circle quality=2 legacy=1"  # legacy gather: identical expected
 [hex1_legacy]="phys=1 legacy=1 shape=hexagon quality=1 dist=5"
)
for m in forward_plus mobile; do
 for k in "${!cases[@]}"; do
  for ed in old new; do
   e=$OLD; [[ $ed == new ]] && e=$NEW
   $S/tools/render.sh $e $m 2560x1440 $out/${m}_${k}_$ed.png ${cases[$k]}
  done
 done
done
python3 - "$out" <<'PY'
import sys, glob, os
import numpy as np
from PIL import Image
sys.path.insert(0, "/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w79/tools")
from edge import measure
out = sys.argv[1]
for p in sorted(glob.glob(out + "/*_old.png")):
    q = p.replace("_old.png", "_new.png")
    a = np.asarray(Image.open(p)).astype(int); b = np.asarray(Image.open(q)).astype(int)
    ro = measure(p, 120)[1]; rn = measure(q, 120)[1]
    print(f"{os.path.basename(p)[:-8]:32s} maxdiff {np.abs(a-b).max():3d}  G radius old {ro['r']:6.2f} new {rn['r']:6.2f}  10-90 old {ro['w1090']:6.2f} new {rn['w1090']:6.2f}")
PY
