#!/usr/bin/env bash
# batch2.sh <new editor> <outdir>: glare renders (starburst, flare, halation, veiling, diffusion) in Forward+ and Mobile.
set -u
S=$(dirname "$0"); NEW=$1; O=$2; mkdir -p $O
R=$S/render.sh
RES=1280x720
for rm in forward_plus mobile; do
  ST="scene=points points=0,0 point_size=0.02 point_energy=200 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.starburst_enabled=true cam.starburst_strength=3000.0"
  for n in 6 7 8; do
    $R $NEW $rm $RES $O/star_${rm}_${n}_f16.png $ST cam.exposure_aperture=16.0 cam.aperture_blade_count=$n
  done
  $R $NEW $rm $RES $O/star_${rm}_6_f8.png $ST cam.exposure_aperture=8.0 cam.aperture_blade_count=6
  $R $NEW $rm $RES $O/star_${rm}_6_f16_rot15.png $ST cam.exposure_aperture=16.0 cam.aperture_blade_count=6 cam.aperture_rotation=15.0
  $R $NEW $rm $RES $O/star_${rm}_off.png scene=points points=0,0 point_size=0.02 point_energy=200 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0
  # lens flare: one source up and to the right
  FL="scene=points points=0.5,0.4 point_size=0.08 point_energy=200 attrs=none env.lens_flare_enabled=true env.lens_flare_intensity=40.0 env.lens_flare_threshold=0.0 env.lens_flare_halo_intensity=0.0 env.lens_flare_chromatic_aberration=0.0"
  $R $NEW $rm $RES $O/flare_${rm}_4.png $FL env.lens_flare_ghost_count=4
  $R $NEW $rm $RES $O/flare_${rm}_look.png scene=points points=0.5,0.4 point_size=0.08 point_energy=200 attrs=none env.lens_flare_enabled=true env.lens_flare_intensity=20.0 env.lens_flare_ghost_count=8 env.lens_flare_halo_intensity=5.0
  # halation: a small bright disc; chain calibration with radius multipliers
  for r in 1.0 4.0 16.0; do
    $R $NEW $rm $RES $O/halation_${rm}_r$r.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0 cam.halation_enabled=true cam.halation_strength=12.0 cam.halation_radius=$r
  done
  $R $NEW $rm $RES $O/halation_${rm}_point.png scene=points points=0,0 point_size=0.03 point_energy=40 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=20.0 cam.exposure_aperture=16.0
  $R $NEW $rm $RES $O/halation_${rm}_flat.png scene=halves card_value=0.5 attrs=practical cam.halation_enabled=true cam.halation_strength=4.0
  # veiling glare and diffusion on a half-bright frame
  $R $NEW $rm $RES $O/halves_${rm}_off.png scene=halves card_value=0.5 attrs=none
  $R $NEW $rm $RES $O/halves_${rm}_veil1.png scene=halves card_value=0.5 attrs=none env.veiling_glare_enabled=true env.veiling_glare_strength=1.0
  $R $NEW $rm $RES $O/halves_${rm}_veil5.png scene=halves card_value=0.5 attrs=none env.veiling_glare_enabled=true env.veiling_glare_strength=5.0
  $R $NEW $rm $RES $O/halves_${rm}_diff1.png scene=halves card_value=0.5 attrs=none env.diffusion_filter_enabled=true env.diffusion_filter_strength=1.0
  $R $NEW $rm $RES $O/halves_${rm}_diffblack1.png scene=halves card_value=0.5 attrs=none env.diffusion_filter_enabled=true env.diffusion_filter_strength=1.0 env.diffusion_filter_black=true
done
