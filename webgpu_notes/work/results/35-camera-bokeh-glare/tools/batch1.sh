#!/usr/bin/env bash
# batch1.sh <new editor> <base editor> <outdir>: defaults (bit-identity) + every effect's measurement renders.
set -u
S=$(dirname "$0"); NEW=$1; BASE=$2; O=$3; mkdir -p $O
R=$S/render.sh
RES=1280x720
PHYS="cam.frustum_focal_length=50.0 cam.frustum_focus_distance=1.0 cam.exposure_aperture=1.4"
# A. defaults, bit-identical against the base editor
[[ "${SKIP_DEFAULTS:-0}" == 1 ]] || for rm in forward_plus mobile gl_compatibility; do
  for a in physical practical none; do
    $R $BASE $rm $RES $O/def_base_${rm}_$a.png scene=default attrs=$a
    $R $NEW  $rm $RES $O/def_new_${rm}_$a.png scene=default attrs=$a
  done
done
for rm in forward_plus mobile; do
  # Mobile's raster DOF ignores the physical model (upstream), so its bokeh shapes are also checked with Practical blur.
  PRAC="attrs=practical cam.dof_blur_far_enabled=true cam.dof_blur_far_distance=2.0 cam.dof_blur_far_transition=1.0 cam.dof_blur_amount=0.5"
  for n in 5 6 7; do
        /pblades___20.png scene=points points=0,0 point_size=0.08 point_energy=20  cam.aperture_enabled=true cam.aperture_blade_count= cam.aperture_rotation=20.0
  done
      /pcatseye_.png scene=points points="0,0;0.45,0;0.9,0;0,0.85;0.6,0.6;-0.9,-0.85" point_size=0.08 point_energy=20  cam.cats_eye_enabled=true cam.aperture_enabled=true cam.aperture_blade_count=0
  # B. blades (far point, defocused): count and rotation
  for n in 5 6 7; do
    for rot in 0 20; do
      $R $NEW $rm $RES $O/blades_${rm}_${n}_${rot}.png scene=points points=0,0 point_size=0.08 point_energy=20 $PHYS cam.aperture_enabled=true cam.aperture_blade_count=$n cam.aperture_rotation=$rot.0
    done
  done
  $R $NEW $rm $RES $O/blades_${rm}_round.png scene=points points=0,0 point_size=0.08 point_energy=20 $PHYS cam.aperture_enabled=true cam.aperture_blade_count=0
  $R $NEW $rm $RES $O/blades_${rm}_6_curved.png scene=points points=0,0 point_size=0.08 point_energy=20 $PHYS cam.aperture_enabled=true cam.aperture_blade_count=6 cam.aperture_blade_curvature=0.5
  $R $NEW $rm $RES $O/blades_${rm}_off.png scene=points points=0,0 point_size=0.08 point_energy=20 $PHYS
  # anamorphic squeeze 2: oval taller than wide
  $R $NEW $rm $RES $O/blades_${rm}_anamorphic.png scene=points points=0,0 point_size=0.08 point_energy=20 $PHYS cam.lens_anamorphic_squeeze=2.0 cam.aperture_enabled=true cam.aperture_blade_count=0
  # C. cat's eye: points across the frame, f/1.4 (= lens max), and stopped down 1 and 2 stops
  PTS="0,0;0.45,0;0.9,0;0,0.85;0.6,0.6;-0.9,-0.85"
  for f in 1.4 2.0 2.8; do
    $R $NEW $rm $RES $O/catseye_${rm}_f$f.png scene=points points="$PTS" point_size=0.08 point_energy=20 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=1.0 cam.exposure_aperture=$f cam.lens_max_aperture=1.4 cam.cats_eye_enabled=true cam.aperture_enabled=true cam.aperture_blade_count=0
  done
  $R $NEW $rm $RES $O/catseye_${rm}_off.png scene=points points="$PTS" point_size=0.08 point_energy=20 $PHYS cam.aperture_enabled=true cam.aperture_blade_count=0
  # D. tilt-shift on a ground plane: none, reverse (miniature), forward; and a vertical axis
  GT="scene=ground cam_height=1.5 cam_pitch=-20 cam.frustum_focal_length=50.0 cam.frustum_focus_distance=5.0 cam.exposure_aperture=1.4 camera.fov=26.99147"
  $R $NEW $rm $RES $O/tilt_${rm}_0.png $GT
  for t in -2.0 -6.0 1.0; do
    $R $NEW $rm $RES $O/tilt_${rm}_$t.png $GT cam.tilt_shift_enabled=true cam.tilt_shift_tilt=$t
  done
  $R $NEW $rm $RES $O/tilt_${rm}_-6.0_axis90.png $GT cam.tilt_shift_enabled=true cam.tilt_shift_tilt=-6.0 cam.tilt_shift_axis=90.0
done
