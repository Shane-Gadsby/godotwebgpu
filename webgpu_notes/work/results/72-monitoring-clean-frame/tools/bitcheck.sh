#!/usr/bin/env bash
# bitcheck.sh <pre editor> <post editor> <outdir>: renders shot.gd variants in 3 renderers with both editors and
# compares. pre renders "plain"/"drawn"/"nr"; post renders the same plus "clean" (compared with pre "drawn") and
# "clean_plain" (compared with pre "plain").
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pre=$1 post=$2 out=$3; mkdir -p "$out"
cmp() {
  python3 -c "
from PIL import Image; import numpy as np
a=np.asarray(Image.open('$1')).astype(int); b=np.asarray(Image.open('$2')).astype(int)
print('$3', 'diff_px', int((a!=b).any(-1).sum()), 'max', int(abs(a-b).max()), 'mean', round(float(a.mean()),2))"
}
shot() { timeout 120 xvfb-run -a -s "-screen 0 2600x1500x24" "$1" --path "$T/bitproj" --rendering-method "$2" --resolution 960x540 -s shot.gd -- "$3" "$4" > "${3%.png}.log" 2>&1; }
for r in forward_plus mobile gl_compatibility; do
  for v in plain drawn nr; do
    shot "$pre" $r "$out/$r-$v-pre.png" $v
    shot "$post" $r "$out/$r-$v-post.png" $v
    cmp "$out/$r-$v-pre.png" "$out/$r-$v-post.png" "$r $v"
  done
  shot "$post" $r "$out/$r-clean-post.png" clean
  cmp "$out/$r-drawn-pre.png" "$out/$r-clean-post.png" "$r clean(post) vs drawn(pre)"
  shot "$post" $r "$out/$r-clean_plain-post.png" clean_plain
  cmp "$out/$r-plain-pre.png" "$out/$r-clean_plain-post.png" "$r clean_plain(post) vs plain(pre)"
done
