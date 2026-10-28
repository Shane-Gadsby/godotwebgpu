#!/bin/bash
# perf.sh: one export per config (config.txt baked in, 2560x1440 viewport), then adapter + canvas check and gpu_time.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w79
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-79/bin/godot.linuxbsd.editor.x86_64
GT=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/perf_guard/gpu_time.mjs
declare -A C=(
 [circ2_on3]="shape=circle quality=2 phys=1 maxblur=3"
 [circ2_on6]="shape=circle quality=2 phys=1"
 [ap6_on6]="shape=hexagon quality=2 phys=1 aperture=6"
 [circ2_legacy]="shape=circle quality=2 phys=1 legacy=1"
 [hex1_on6]="shape=hexagon quality=1 phys=1"
)
ONLY=${ONLY:-${!C[@]}}
for k in $ONLY; do
	echo "${C[$k]}" > $S/webproj/config.txt
	[[ -d $S/perf/$k ]] || $S/tools/export.sh $NEW $S/webproj $S/perf/$k | tail -1
done
for k in $ONLY; do
	node $S/tools/canvas_size.mjs $S/perf/$k 2560x1440 || { echo "$k ADAPTER/CANVAS FAILED"; continue; }
	r=$(timeout ${TMO:-600} node $GT --dir $S/perf/$k --uncapped --rounds 3 --size 2560x1440 2>&1 | grep -E "GPU busy|frames |Error|rror" | tr "\n" " ")
	echo "$k rc=$? $r"
done
