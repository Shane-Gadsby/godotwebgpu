#!/bin/bash
# run_native.sh <godot binary> <label>: wb_check on lavapipe/llvmpipe under xvfb in Forward+, Mobile, Compatibility.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/86fd2a38-7e49-49e0-a3fd-d31362379e4b/scratchpad
BIN=$1; LABEL=$2; P=$S/wbproj
"$BIN" --headless --path $P --import >/dev/null 2>&1
for m in forward_plus mobile gl_compatibility; do
  out=$S/wb/out/$LABEL/$m; mkdir -p $out
  drv=vulkan; [[ $m == gl_compatibility ]] && drv=opengl3
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json LIBGL_ALWAYS_SOFTWARE=1 timeout 120 xvfb-run -a -s "-screen 0 640x480x24" "$BIN" --path $P --rendering-method $m --rendering-driver $drv -- --out=$out 2>&1 | grep "\[WB\]\|ERROR" | sed "s/^/$LABEL $m: /"
done
