#!/usr/bin/env bash
# gpu_job.sh: waits for the gpu lease, then: native 8-bit counterparts of the web cases, the web captures, GPU timings,
# a white-light AWB check; drops the lease. Writes $W/out/gpu_job.log.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w36
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-36/webgpu_notes/work/lease.sh
E=/mnt/109313D2109313D2/godot-editors/godotwebgpu-36/bin/godot.linuxbsd.editor.x86_64
until $L take gpu worker-36 2 >/dev/null 2>&1; do sleep 30; done
echo "lease taken $(date)"
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader
O=$W/out/native8
mkdir -p $O
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"
n() { local name=$1; shift; $W/r.sh $E forward_plus 1280x720 out=$O/$name "$@" | grep -vE "xic|GPUMS"; }
n meter_ref scene=meter caps=90
n meter_avg scene=meter caps=90 $AE
n meter_center scene=meter caps=90 $AE auto_exposure_metering_mode=1
n meter_spot scene=meter caps=90 $AE auto_exposure_metering_mode=2
n meter_spotwin scene=meter caps=90 $AE auto_exposure_metering_mode=2 auto_exposure_metering_spot_point=0.75,0.3 auto_exposure_metering_spot_size=1.0
n meter_hl99 scene=meter caps=90 $AE auto_exposure_metering_mode=3
n meter_hl90 scene=meter caps=90 $AE auto_exposure_metering_mode=3 auto_exposure_metering_highlight_percentile=90
n awb_ref scene=awb exposure=0.5 caps=12
n awb_on scene=awb exposure=0.5 caps=500 white_balance_auto=true white_balance_auto_speed=3
n awb_clamp scene=awb exposure=0.5 caps=500 white_balance_auto=true white_balance_auto_speed=3 white_balance_auto_min_temperature=4000
n awb_bias scene=awb exposure=0.5 caps=500 white_balance_auto=true white_balance_auto_speed=3 white_balance_temperature=5000 white_balance_tint=20
n default_prac scene=default attrs=practical caps=12
n default_ae scene=default attrs=practical caps=12 auto_exposure_enabled=true
echo "== white light AWB (HDR)"
$W/r.sh $E forward_plus 1280x720 scene=awb hdr=1 exposure=0.5 light=1,1,1 caps=12 out=$W/out/awb_fp/white_ref | grep -vE "xic|GPUMS"
$W/r.sh $E forward_plus 1280x720 scene=awb hdr=1 exposure=0.5 light=1,1,1 caps=400 out=$W/out/awb_fp/white_awb white_balance_auto=true white_balance_auto_speed=3 | grep -vE "xic|GPUMS"
$W/r.sh $E forward_plus 1280x720 scene=awb hdr=1 exposure=0.5 light=1,0.94292063,0.99215158 caps=400 out=$W/out/awb_fp/d6500_awb white_balance_auto=true white_balance_auto_speed=3 | grep -vE "xic|GPUMS"
echo "== web"
(cd $W && timeout 2400 node shoot.mjs $W/webexport $W/out/web 2>&1 | tail -40)
echo "== GPU time (ms, median of frames 21-400, 1920x1080, AWB room)"
$W/perf_runs.sh $E
$L drop gpu worker-36
echo "GPU_JOB_DONE $(date)"
