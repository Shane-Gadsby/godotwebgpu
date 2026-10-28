#!/usr/bin/env bash
# perf40.sh <base editor> <new editor>: median viewport GPU time per case, 1920x1080, scene=awb (caller holds gpu).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w40/t36
mkdir -p $W/out
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"
for rep in 1 2 3; do
for c in "base:none:" "base:avg:$AE" "new:none:" "new:avg:$AE" "new:logavg:$AE auto_exposure_metering_log_average=true" "new:logcenter:$AE auto_exposure_metering_mode=1 auto_exposure_metering_log_average=true" "base:halation:halation_enabled=true" "new:halation:halation_enabled=true" "new:halation_hq:halation_enabled=true halation_high_quality=true"; do
	ed=${c%%:*}; rest=${c#*:}; name=${rest%%:*}; args=${rest#*:}
	E=$1; [[ $ed == new ]] && E=$2
	printf "%-5s %-14s " $ed $name; $W/r.sh $E forward_plus 1920x1080 scene=awb caps=400 maxfps=0 out=$W/out/perf_$name $args | grep GPUMS
done
done
