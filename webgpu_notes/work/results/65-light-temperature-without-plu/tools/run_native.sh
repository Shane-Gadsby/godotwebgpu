#!/bin/bash
# run_native.sh <godot binary> <label> <project dir> <set> : lt.gd in Forward+, Mobile, Compatibility on the real GPU
# (Vulkan; OpenGL for Compatibility) under xvfb. Writes out/<label>/<renderer>/*.exr and out/<label>/<renderer>.txt.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w64
BIN=$1; LABEL=$2; P=$3; SET=$4
"$BIN" --headless --path $P --import >/dev/null 2>&1
for m in forward_plus mobile gl_compatibility; do
	out=$S/out/$LABEL/$m; mkdir -p $out
	drv=vulkan; [[ $m == gl_compatibility ]] && drv=opengl3
	timeout 180 xvfb-run -a -s "-screen 0 640x480x24" "$BIN" --path $P --rendering-method $m --rendering-driver $drv -- --out=$out --set=$SET 2>&1 | grep "\[LT\]\|ERROR\|SCRIPT" > $out.txt
	echo "$LABEL $m: $(grep -c '^\[LT\] [a-z0-9_]* [0-9]' $out.txt) cases, $(grep -c 'ERROR\|SCRIPT' $out.txt) errors; $(head -1 $out.txt)"
done
