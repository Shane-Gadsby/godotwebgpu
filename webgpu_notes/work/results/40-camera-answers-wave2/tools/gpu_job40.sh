#!/usr/bin/env bash
# gpu_job40.sh: export both web projects, then under the gpu lease: native 8-bit counterparts, web captures, compare.
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w40
L=/mnt/109313D2109313D2/godot-editors/godotwebgpu-40/webgpu_notes/work/lease.sh
E=$S/editor_new3
PY=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/venv/bin/python
cp /mnt/109313D2109313D2/godot-editors/godotwebgpu-40/bin/godot.web.template_debug.wasm32.nothreads.zip $S/web_template_40.zip || exit 1
for t in 35 36; do
	rm -rf $S/webexport$t; mkdir -p $S/webexport$t
	timeout 600 $E --headless --path $S/webproj$t --export-debug WebGPU $S/webexport$t/index.html > $S/export$t.log 2>&1
	ls $S/webexport$t/index.html >/dev/null || { echo "export $t failed"; tail $S/export$t.log; exit 1; }
done
until $L take gpu worker-40 2 >/dev/null; do sleep 30; done
echo "gpu lease $(date)"; nvidia-smi --query-gpu=memory.used --format=csv,noheader
O=$S/nat8; mkdir -p $O
AE="auto_exposure_enabled=true auto_exposure_max_sensitivity=1000000 auto_exposure_speed=20"; LG="auto_exposure_metering_log_average=true"
n() { local name=$1; shift; $S/t36/r.sh $E forward_plus 1280x720 out=$O/$name "$@" | grep -vE "xic|GPUMS"; }
n meter_ref scene=meter caps=90
n meter_avg scene=meter caps=90 $AE
n meter_logavg scene=meter caps=90 $AE $LG
n meter_logcenter scene=meter caps=90 $AE $LG auto_exposure_metering_mode=1
n meter_logspotwin scene=meter caps=90 $AE $LG auto_exposure_metering_mode=2 auto_exposure_metering_spot_point=0.75,0.3 auto_exposure_metering_spot_size=1.0
n default_ae scene=default attrs=practical caps=12 auto_exposure_enabled=true
P="scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0"
H="cam.halation_enabled=true cam.halation_strength=12.0"
g() { local name=$1; shift; $S/t35/render.sh $E forward_plus 1280x720 $O/$name.12.png "$@"; }
g halo_point $P
g halo_r1 $P $H cam.halation_radius=1.0
g halo_hq_r1 $P $H cam.halation_radius=1.0 cam.halation_high_quality=true
g halo_hq_r0.5 $P $H cam.halation_radius=0.5 cam.halation_high_quality=true
g halo_hq_r4 $P $H cam.halation_radius=4.0 cam.halation_high_quality=true
SB="scene=points points=0,0 point_size=0.02 point_energy=200 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.starburst_enabled=true cam.aperture_blade_count=6 cam.exposure_aperture=16.0"
g star_default $SB
g star_20 $SB cam.starburst_strength=20.0
g star_1 $SB cam.starburst_strength=1.0
echo "== web"
cd $S
timeout 1200 node shoot40.mjs $S/webexport36 $S/web $S/cases36.json 2>&1 | tail -15
timeout 1200 node shoot40.mjs $S/webexport35 $S/web $S/cases35.json 2>&1 | tail -15
$L drop gpu worker-40
$PY $S/t36/compare_web.py $O $S/web
echo GPU_JOB_DONE
