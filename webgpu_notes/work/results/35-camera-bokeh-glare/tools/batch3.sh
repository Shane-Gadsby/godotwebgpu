#!/usr/bin/env bash
set -u
S=$(dirname "$0"); NEW=$1; O=$2; mkdir -p $O; R=$S/render.sh; RES=1280x720
for rm in forward_plus mobile; do
  GT="scene=ground cam_height=1.5 cam_pitch=-20 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=3.0 cam.exposure_aperture=1.4"
  $R $NEW $rm $RES $O/tilt_${rm}_ref.png scene=ground cam_height=1.5 cam_pitch=-20 attrs=none camera.fov=26.99147
  $R $NEW $rm $RES $O/tilt_${rm}_0.png $GT
  for t in -2.0 -6.0 1.0; do $R $NEW $rm $RES $O/tilt_${rm}_$t.png $GT cam.tilt_shift_enabled=true cam.tilt_shift_tilt=$t; done
  for r in 1.0 4.0 16.0; do
    $R $NEW $rm $RES $O/halation_${rm}_r$r.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0 cam.halation_enabled=true cam.halation_strength=12.0 cam.halation_radius=$r
  done
  $R $NEW $rm $RES $O/halation_${rm}_point.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0
  ST="scene=points points=0,0 point_size=0.02 point_energy=200 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.starburst_enabled=true cam.starburst_strength=400.0 cam.aperture_blade_count=6"
  $R $NEW $rm $RES $O/star_${rm}_6_f16.png $ST cam.exposure_aperture=16.0
  $R $NEW $rm $RES $O/star_${rm}_6_f8.png $ST cam.exposure_aperture=8.0
done
