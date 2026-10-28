#!/usr/bin/env bash
# perf_runs.sh <editor>: median GPU frame time (viewport measured render time) per case, 1920x1080 (caller holds gpu).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w36
E=$1
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"
for rep in 1 2; do
for c in "none:" "avg:$AE" "center:$AE auto_exposure_metering_mode=1" "spot:$AE auto_exposure_metering_mode=2" "highlight:$AE auto_exposure_metering_mode=3" "awb:white_balance_auto=true" "center+awb:$AE auto_exposure_metering_mode=1 white_balance_auto=true"; do
	name=${c%%:*}; args=${c#*:}
	printf "%-12s " $name; $W/r.sh $E forward_plus 1920x1080 scene=awb caps=400 maxfps=0 out=$W/out/perf_$name $args | grep GPUMS
done
done
