#!/usr/bin/env bash
# halo_runs.sh <editor> <outdir> [renderers] [res]: halation point renders, HDR, with and without high quality.
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w40
E=$1 O=$2 RMS=${3:-forward_plus} RES=${4:-1280x720}; mkdir -p $O; R=$W/t35/render.sh
P="scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0 hdr=1"
for rm in $RMS; do
  $R $E $rm $RES $O/halation_${rm}_point.png $P
  for r in 0.5 1.0 2.0 4.0 16.0; do
    $R $E $rm $RES $O/halation_${rm}_r$r.png $P cam.halation_enabled=true cam.halation_strength=12.0 cam.halation_radius=$r
    $R $E $rm $RES $O/halation_${rm}_hq_r$r.png $P cam.halation_enabled=true cam.halation_strength=12.0 cam.halation_radius=$r cam.halation_high_quality=true
  done
done
echo HALO_DONE
