#!/usr/bin/env bash
set -u
S=$(dirname "$0"); NEW=$1; BASE=$2; O=$3; mkdir -p $O; R=$S/render.sh; RES=1280x720
for rm in forward_plus mobile gl_compatibility; do
  for a in physical practical none; do
    $R $BASE $rm $RES $O/def_base_${rm}_$a.png scene=default attrs=$a
    $R $NEW  $rm $RES $O/def_new_${rm}_$a.png scene=default attrs=$a
  done
done
for rm in forward_plus mobile; do
  for r in 1.0 4.0 16.0; do
    $R $NEW $rm $RES $O/halation_${rm}_r$r.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0 cam.halation_enabled=true cam.halation_strength=12.0 cam.halation_radius=$r
  done
  $R $NEW $rm $RES $O/halation_${rm}_point.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0
  $R $NEW $rm $RES $O/halation_${rm}_flat.png scene=halves card_value=0.5 attrs=practical cam.halation_enabled=true cam.halation_strength=4.0
  $R $NEW $rm $RES $O/halves_${rm}_off.png scene=halves card_value=0.5 attrs=none
  $R $NEW $rm $RES $O/halves_${rm}_diff1.png scene=halves card_value=0.5 attrs=none env.diffusion_filter_enabled=true env.diffusion_filter_strength=1.0
  $R $NEW $rm $RES $O/halves_${rm}_diffblack1.png scene=halves card_value=0.5 attrs=none env.diffusion_filter_enabled=true env.diffusion_filter_strength=1.0 env.diffusion_filter_black=true
done
