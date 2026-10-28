#!/usr/bin/env bash
# Native controller runs on the real display (timing), and the FSR 2 captures around a controller change (Xvfb).
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w75
R=$S/out/native; mkdir -p $R
run() { "$S/bench/run_native.sh" "$@" || echo "FAIL $*"; }
case "${1:-all}" in
ctrl|all)
	# GPU-bound, the load drops at 45 s.
	run new $R/gpu_bil.txt forward_plus display mode=ctrl iters=4500 dur=90 load2_at=45 iters2=1500
	# CPU-bound: 24 ms busy-wait per frame, light GPU.
	run new $R/cpu_bil.txt forward_plus display mode=ctrl iters=64 cpu_ms=24 dur=60
	# GPU-bound with FSR 2.
	run new $R/gpu_fsr2.txt forward_plus display mode=ctrl up=fsr2 iters=4500 dur=60
	;;&
cap|all)
	# FSR 2 around a controller change: down (target raised 30 -> 100 fps) and up (starts at 0.6 with room).
	run new $R/cap_fsr2_down.txt forward_plus xvfb mode=cap up=fsr2 iters=3000 target=30 target2=100 warm=300 frames=30 change_at=10
	run new $R/cap_fsr2_up.txt forward_plus xvfb mode=cap up=fsr2 iters=3000 target=30 target2=30 s0=0.6 warm=30 frames=30 change_at=10
	run new $R/cap_fsr2_ae_down.txt forward_plus xvfb mode=cap up=fsr2 ae=1 iters=3000 target=30 target2=100 warm=300 frames=30 change_at=10
	;;
esac
echo BATCH DONE
