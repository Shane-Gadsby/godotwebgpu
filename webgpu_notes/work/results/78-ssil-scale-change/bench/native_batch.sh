#!/usr/bin/env bash
# Task 78 native runs: change captures (base and new) with SSIL, and the no-change bit-identical set.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w78
R=$S/native; mkdir -p $R
run() { "$S/run_native.sh" "$@" || echo "FAIL $*"; }
W="warm=240 frames=40 change_at=8"
for b in base new; do
	run $b $R/chg_ssil_down_$b.txt forward_plus up=bilinear ssil=1 ssr=0 s0=1.0 s1=0.6 $W
	run $b $R/chg_ssil_up_$b.txt forward_plus up=bilinear ssil=1 ssr=0 s0=0.6 s1=1.0 $W
	run $b $R/chg_ssil_ssr_down_$b.txt forward_plus up=bilinear ssil=1 ssr=1 s0=1.0 s1=0.6 $W
	run $b $R/chg_ssil_taa_down_$b.txt forward_plus up=taa ssil=1 ssr=0 s0=1.0 s1=0.6 $W
	run $b $R/chg_ssil_taa_up_$b.txt forward_plus up=taa ssil=1 ssr=0 s0=0.6 s1=1.0 $W
	run $b $R/chg_ssil_fsr2_down_$b.txt forward_plus up=fsr2 ssil=1 ssr=0 s0=1.0 s1=0.6 $W
	run $b $R/chg_ssr_down_$b.txt forward_plus up=bilinear ssil=0 ssr=1 s0=1.0 s1=0.6 $W
	run $b $R/chg_ref_down_$b.txt forward_plus up=bilinear ssil=0 ssr=0 s0=1.0 s1=0.6 $W
	NC="warm=120 frames=12 change_at=100"
	for cfg in "up=bilinear ssil=1 ssr=0 s0=1.0" "up=bilinear ssil=1 ssr=0 s0=0.77" "up=bilinear ssil=1 ssr=1 s0=0.77" "up=taa ssil=1 ssr=0 s0=0.77" "up=fsr2 ssil=1 ssr=0 s0=0.77" "up=bilinear ssil=0 ssr=1 s0=0.77"; do
		n=$(echo "$cfg" | tr ' =.' '___')
		run $b $R/nc_fp_${n}_$b.txt forward_plus $cfg s1=${cfg##*s0=} $NC
	done
	run $b $R/nc_mob_bil_$b.txt mobile up=bilinear s0=0.77 s1=0.77 $NC
done
run new $R/nc_fp_ssil_077_new2.txt forward_plus up=bilinear ssil=1 ssr=0 s0=0.77 s1=0.77 warm=120 frames=12 change_at=100
echo BATCH_DONE
