#!/bin/bash
# bitident.sh <outdir>: renders the default/opt-in-off cases with the pre-change and new editors, Forward+ and Mobile.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w79
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-79/bin/godot.linuxbsd.editor.x86_64
OLD=$S/godot_pre79
out=$1; mkdir -p $out
declare -A cases=(
 [phys_hex1]="shape=hexagon quality=1"
 [phys_circ2]="shape=circle quality=2"
 [phys_box3_near]="shape=box quality=3 focus=8 dist=3"
 [pract_hex2]="kind=practical shape=hexagon quality=2 trans=0.5 amount=0.17"
 [pract_circ2_phys]="kind=practical shape=circle quality=2 amount=0.17"
 [optin_hex1_max3]="phys=1 shape=hexagon quality=1 maxblur=3"
 [optin_circ2_max3_long]="phys=1 shape=circle quality=2 f=200 n=2.8 focus=5 dist=50 maxblur=3"
)
for m in forward_plus mobile; do
 for k in "${!cases[@]}"; do
  for ed in old new; do
   e=$OLD; [[ $ed == new ]] && e=$NEW
   $S/tools/render.sh $e $m 1280x720 $out/${m}_${k}_$ed.png ${cases[$k]}
  done
 done
done
python3 - "$out" <<'PY'
import sys, glob, os
import numpy as np
from PIL import Image
out = sys.argv[1]
bad = 0
for p in sorted(glob.glob(out + "/*_old.png")):
    q = p.replace("_old.png", "_new.png")
    if not os.path.exists(q) or not os.path.exists(p):
        print("MISSING", p); bad += 1; continue
    a = np.asarray(Image.open(p)).astype(int); b = np.asarray(Image.open(q)).astype(int)
    d = np.abs(a - b).max()
    print(os.path.basename(p)[:-8], "maxdiff", d, "mean", round(float(a.mean()), 3))
    bad += d != 0
print("NONZERO", bad)
PY
