#!/bin/bash
# identical.sh <base editor> <new editor> <outdir>: micro shadows off (and on without an AO map, and at opacity 0)
# must render exactly as the base editor.
B=$1; N=$2; od=$3; mkdir -p "$od"; d=$(cd "$(dirname "$0")"; pwd)
for m in forward_plus mobile gl_compatibility; do
  for cfg in "elev=40 shadow=0" "elev=25 shadow=1 occluder=1" "elev=60 shadow=1 occluder=1 ao=0" "elev=40 shadow=0 normal=0"; do
    tag=$(echo "$cfg" | tr ' =' '_-')
    "$d/render.sh" "$B" $m "$od/base_${m}_$tag" $cfg micro=0 || exit 1
    "$d/render.sh" "$N" $m "$od/new_${m}_$tag" $cfg micro=0 || exit 1
    "$d/render.sh" "$N" $m "$od/new0_${m}_$tag" $cfg micro=1 opacity=0 || exit 1
    extra=""
    if [[ "$cfg" == *ao=0* ]]; then
      "$d/render.sh" "$N" $m "$od/new1_${m}_$tag" $cfg micro=1 opacity=1 || exit 1
      extra="$od/new1_${m}_$tag.raw"
    fi
    python3 - "$od/base_${m}_$tag.raw" "$od/new_${m}_$tag.raw" "$od/new0_${m}_$tag.raw" $extra <<'PY'
import sys
import numpy as np
a = np.fromfile(sys.argv[1], dtype=np.float32)
for p in sys.argv[2:]:
    b = np.fromfile(p, dtype=np.float32)
    print(f"  {p.split('/')[-1]}: max diff {np.abs(a - b).max():.6g}, differing values {(a != b).sum()}")
PY
  done
done
