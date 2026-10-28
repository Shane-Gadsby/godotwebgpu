#!/bin/bash
# perf.sh: one export per config (config.txt baked in, 2560x1440 viewport), then adapter + canvas check and gpu_time.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w70
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-70/bin/godot.linuxbsd.editor.x86_64
GT=/mnt/109313D2109313D2/godot-editors/godotwebgpu/webgpu_tests/perf_guard/gpu_time.mjs
declare -A C=(
 [hex1_off]="shape=hexagon quality=1"
 [hex1_on3]="shape=hexagon quality=1 phys=1 maxblur=3"
 [hex1_on6]="shape=hexagon quality=1 phys=1"
 [hex1_legacy]="shape=hexagon quality=1 phys=1 legacy=1"
 [hex3_off]="shape=hexagon quality=3"
 [hex3_on3]="shape=hexagon quality=3 phys=1 maxblur=3"
 [hex3_on6]="shape=hexagon quality=3 phys=1"
 [circ2_off]="shape=circle quality=2"
 [circ2_on3]="shape=circle quality=2 phys=1 maxblur=3"
 [circ2_on6]="shape=circle quality=2 phys=1"
 [circ2_legacy]="shape=circle quality=2 phys=1 legacy=1"
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
