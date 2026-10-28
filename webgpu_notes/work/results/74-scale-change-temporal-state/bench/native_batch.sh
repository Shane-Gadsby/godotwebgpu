#!/usr/bin/env bash
# All native runs: change captures (base and new) and the no-change bit-identical set.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w74
R=$S/native; mkdir -p $R
run() { "$S/run_native.sh" "$@" || echo "FAIL $*"; }
W="warm=240 frames=40 change_at=8"
for b in base new; do
	# Changes.
	run $b $R/chg_fsr2_down_$b.txt forward_plus up=fsr2 s0=1.0 s1=0.6 $W
	run $b $R/chg_fsr2_up_$b.txt forward_plus up=fsr2 s0=0.6 s1=1.0 $W
	run $b $R/chg_taa_down_$b.txt forward_plus up=taa s0=1.0 s1=0.6 $W
	run $b $R/chg_taa_up_$b.txt forward_plus up=taa s0=0.6 s1=1.0 $W
	run $b $R/chg_bil_ae_down_$b.txt forward_plus up=bilinear ae=1 s0=1.0 s1=0.6 $W
	run $b $R/chg_bil_ae_up_$b.txt forward_plus up=bilinear ae=1 s0=0.6 s1=1.0 $W
	run $b $R/chg_fsr2_ae_down_$b.txt forward_plus up=fsr2 ae=1 s0=1.0 s1=0.6 $W
	run $b $R/chg_bil_down_$b.txt forward_plus up=bilinear s0=1.0 s1=0.6 $W
	run $b $R/chg_mob_ae_down_$b.txt mobile up=bilinear ae=1 s0=1.0 s1=0.6 $W
	# No change: bit-identical set.
	NC="warm=120 frames=12 change_at=100"
	for cfg in "up=bilinear s0=1.0" "up=bilinear s0=0.77" "up=fsr2 s0=0.77" "up=fsr2 s0=1.0" "up=taa s0=0.77" "up=bilinear ae=1 s0=0.77" "up=fsr2 ae=1 s0=0.77"; do
		n=$(echo "$cfg" | tr ' =.' '___')
		run $b $R/nc_fp_${n}_$b.txt forward_plus $cfg s1=${cfg##*s0=} $NC
	done
	for cfg in "up=bilinear s0=1.0" "up=bilinear s0=0.77" "up=bilinear ae=1 s0=0.77"; do
		n=$(echo "$cfg" | tr ' =.' '___')
		run $b $R/nc_mob_${n}_$b.txt mobile $cfg s1=${cfg##*s0=} $NC
	done
done
# Determinism of the no-change runs: the new build twice.
run new $R/nc_fp_up_fsr2_s0_0_77_new2.txt forward_plus up=fsr2 s0=0.77 s1=0.77 warm=120 frames=12 change_at=100
run new $R/nc_fp_up_bilinear_ae_1_s0_0_77_new2.txt forward_plus up=bilinear ae=1 s0=0.77 s1=0.77 warm=120 frames=12 change_at=100
echo BATCH_DONE
