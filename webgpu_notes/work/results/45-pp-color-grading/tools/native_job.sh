#!/bin/bash
# All native renders for task 45 (run under the gpu lease) and the reference checks.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w45
E=$W/work_editor
B=$W/base_editor
CASES="neutral filter hue_shift vibrance vibrance_neg lift gamma gain smh mixer split curve_master curve_rgb hue_vs_hue hue_vs_sat sat_vs_sat lum_vs_sat all all_user3d all_user1d all_adjust"
for m in forward_plus mobile gl_compatibility; do
	args=()
	for c in off user3d_only user1d_only $CASES; do args+=("$c:lattice"); done
	for c in off neutral all hue_shift vibrance curve_master; do args+=("$c:off"); done
	$W/native.sh $E $m $W/out/$m "${args[@]}"
	$W/native.sh $B $m $W/out/base_$m off:lattice off:off user3d_only:lattice user1d_only:lattice
done
mkdir -p $W/out/look
for l in off teal_orange bleach warm; do for m in forward_plus mobile gl_compatibility; do
	timeout 120 xvfb-run -a -s "-screen 0 1400x900x24" $E --path $W/proj --rendering-method $m --resolution 1280x720 -s look.gd -- out=$W/out/look/${l}_$m.png look=$l > $W/out/look/${l}_$m.log 2>&1 || echo "FAILED look $l $m"
done; done
echo NATIVE_DONE
