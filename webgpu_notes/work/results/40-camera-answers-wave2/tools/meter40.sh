#!/usr/bin/env bash
# meter40.sh <editor> <outdir> [log]: Average (and with "log", every mode with the log-average) on task 36's meter
# scene, HDR captures at 1080p and 720p (caller holds the gpu lease).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w40/t36
E=$1 O=$2 LOG=${3:-}
mkdir -p $O
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"
L="auto_exposure_metering_log_average=true"
for res in 1920x1080 1280x720; do
	$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/ref_$res | grep -v xic
	$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/avg_$res $AE | grep -v xic
	if [[ -n "$LOG" ]]; then
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/logavg_$res $AE $L | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/logcenter_$res $AE $L auto_exposure_metering_mode=1 | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/logspot_$res $AE $L auto_exposure_metering_mode=2 | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/logspotwin_$res $AE $L auto_exposure_metering_mode=2 auto_exposure_metering_spot_point=0.75,0.3 auto_exposure_metering_spot_size=1.0 | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/center_$res $AE auto_exposure_metering_mode=1 | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/hl99_$res $AE auto_exposure_metering_mode=3 | grep -v xic
		$W/r.sh $E forward_plus $res scene=meter hdr=1 caps=30 out=$O/loghl99_$res $AE $L auto_exposure_metering_mode=3 | grep -v xic
	fi
done
echo METER_DONE
