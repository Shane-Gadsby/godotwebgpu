#!/usr/bin/env bash
# run_native.sh <editor> <outdir>: the suite in Forward+, Mobile and Compatibility under xvfb (Vulkan / OpenGL).
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ed=$1 out=$2; mkdir -p "$out"
for r in forward_plus mobile gl_compatibility; do
  timeout 300 xvfb-run -a -s "-screen 0 2600x1500x24" "$ed" --path "$T/project" --rendering-method $r --resolution 640x360 -- --mode=suite --out="$out/$r" > "$out/$r.log" 2>&1
  echo "== $r exit $?"; grep -E "^RESULT|ERROR|SCRIPT ERROR" "$out/$r.log"
done
