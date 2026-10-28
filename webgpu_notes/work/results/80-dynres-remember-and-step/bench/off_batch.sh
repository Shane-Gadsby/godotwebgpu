#!/usr/bin/env bash
# Off = bit-identical: the same no-controller renders with the pre-change and the new editor.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w80
R=$S/out/off; mkdir -p $R
N="mode=off dyn=0 warm=120 frames=12"
for b in pre new; do
	for cfg in "up=bilinear s0=1.0" "up=bilinear s0=0.77" "up=fsr2 s0=0.77" "up=fsr2 s0=1.0" "up=taa s0=0.77" "up=bilinear ae=1 s0=0.77" "up=fsr2 ae=1 s0=0.77"; do
		n=$(echo "$cfg" | tr ' =.' '___')
		$S/bench/run_native.sh $b $R/fp_${n}_$b.txt forward_plus xvfb $cfg $N || echo "FAIL $b $cfg"
	done
	for cfg in "up=bilinear s0=0.77" "up=bilinear ae=1 s0=0.77"; do
		n=$(echo "$cfg" | tr ' =.' '___')
		$S/bench/run_native.sh $b $R/mob_${n}_$b.txt mobile xvfb $cfg $N || echo "FAIL $b mobile $cfg"
	done
done
cd $R; for f in *_pre.txt; do g=${f%_pre.txt}_new.txt; a=$(grep -o '"md5":\[[^]]*\]' $f); b=$(grep -o '"md5":\[[^]]*\]' $g); if [[ -n "$a" && "$a" == "$b" ]]; then echo "IDENTICAL ${f%_pre.txt} $(echo $a | tr ',' '\n' | sort -u | wc -l) distinct"; else echo "DIFFERENT ${f%_pre.txt}"; fi; done
