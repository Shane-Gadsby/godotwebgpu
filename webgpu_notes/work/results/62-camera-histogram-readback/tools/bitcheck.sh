#!/usr/bin/env bash
# bitcheck.sh <pre editor> <post editor> <outdir>: renders shot.gd variants in 3 renderers with both, compares.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w62
pre=$1 post=$2 out=$3; mkdir -p $out
for r in forward_plus mobile gl_compatibility; do
  for v in plain drawn nr; do
    for e in pre post; do
      bin=$pre; [[ $e == post ]] && bin=$post
      timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" $bin --path $S/bitproj --rendering-method $r --resolution 960x540 -s shot.gd -- $out/$r-$v-$e.png $v > $out/$r-$v-$e.log 2>&1
    done
    python3 -c "
from PIL import Image; import numpy as np, sys
a=np.asarray(Image.open('$out/$r-$v-pre.png')).astype(int); b=np.asarray(Image.open('$out/$r-$v-post.png')).astype(int)
print('$r $v', 'diff_px', int((a!=b).any(-1).sum()), 'max', int(abs(a-b).max()), 'mean', round(float(a.mean()),2))"
  done
done
