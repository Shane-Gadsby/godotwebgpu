#!/usr/bin/env bash
# meter_runs.sh <editor> <outdir> [renderer]: the metering cases, HDR captures (caller holds the gpu lease).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w36
E=$1 O=$2 RM=${3:-forward_plus}
mkdir -p $O
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"
for res in 1920x1080 1280x720; do
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/ref_$res | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/avg_$res $AE | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/center_$res $AE auto_exposure_metering_mode=1 | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/spot_$res $AE auto_exposure_metering_mode=2 | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/spotwin_$res $AE auto_exposure_metering_mode=2 auto_exposure_metering_spot_point=0.75,0.3 auto_exposure_metering_spot_size=1.0 | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/hl99_$res $AE auto_exposure_metering_mode=3 | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/hl90_$res $AE auto_exposure_metering_mode=3 auto_exposure_metering_highlight_percentile=90 | grep -v xic
	$W/r.sh $E $RM $res scene=meter hdr=1 caps=30 out=$O/hl999_$res $AE auto_exposure_metering_mode=3 auto_exposure_metering_highlight_percentile=99.9 auto_exposure_scale=0.8 | grep -v xic
done
echo METER_DONE
