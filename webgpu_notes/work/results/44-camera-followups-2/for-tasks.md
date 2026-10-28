# For TASKS.md (task 44)

## §4 entry

### 4.5x Longitudinal chromatic aberration; Mobile physical DOF; camera arms in the Forward+ matrix (October 2026) *(work task 44, camera batch)*

- **Longitudinal CA** (CameraAttributes `longitudinal_chromatic_aberration_enabled/_strength`, RS
  `camera_attributes_set_longitudinal_chromatic_aberration`): red and blue get their own circle of confusion in the
  DOF pass (Forward+ and Mobile; Compatibility has no DOF). Physical: achromat secondary spectrum f/2200 (Smith 5.3),
  thin-lens offset k u / f x the blur size, constant through the focus; Practical: red/blue blur x (1 -/+ 0.027 x
  strength). Bokeh push constant's `pad` -> `longitudinal_ca` (half2), flag bit 11 of `shape_flags`. Verified on a
  defocused edge: fringe 2.10 px vs 2.13 modeled at f/1.4, half that at f/2.8, green behind / magenta in front; off
  bit-identical (Forward+/Mobile/Compatibility x shapes x qualities).
- **Mobile physical DOF fixed** (hand-offs `mobile-dof-physical`, default "Fix it"): `bokeh_dof_raster()` now takes
  the physical flags and the 32 px cap from a negative transition, as `bokeh_dof_compute()` does. Before, a Physical
  camera had no blur on Mobile (edge radius 0.00); now 8.15 / 4.16 px vs Forward+ 7.89 / 4.03 (model 7.81 / 3.91).
  Existing Mobile + Physical renders change.
- **Forward+ matrix 87 -> 97 arms** (category `camera`): lens_optics, bokeh_shape, longitudinal_ca, tilt_shift,
  starburst, halation, lens_flare, veiling_glare_diffusion, camera_lut, in_camera_processing. Unbaked and baked parity
  10/10; full unbaked parity 97/97.
- Interface reference: `(3, 120)` push-constant emulation added to `bokeh_dof_raster` composite fragment.

## Arm count to update

CLAUDE.md "Forward+ feature matrix, 87 arms" -> **97 arms** (webgpu_tests/README.md already updated on the branch).

## Remove from §4.52 / §4.54 follow-ups

- §4.52 "Follow-ups: a Forward+ matrix arm `lens_optics`; longitudinal CA in the DOF pass": both done.
- §4.54 "Upstream problem found, not fixed: Mobile's DOF ignores CameraAttributesPhysical": fixed.

## Questions for the user

None new. `mobile-dof-physical` was built with its default ("Fix it"); if the user picks otherwise, revert the
`use_physical_*` block in `bokeh_dof_raster()`.
