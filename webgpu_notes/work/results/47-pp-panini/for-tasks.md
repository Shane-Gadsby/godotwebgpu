# Task 47: for TASKS.md

## §4 entry

### 4.xx Panini projection (October 2026) *(work task 47, post-processing picks)*

`CameraAttributes.panini_enabled / panini_distance / panini_vertical_compression` (Sharpless, Ballestrem and Thomas
2010), in task 33's lens resample of the tonemap pass (header.x bit 32; `lens[2].zw`, `lens[4].zw`, `lens[7].zw`; no
layout change, no new binding), all three renderers and WebGPU, off by default and bit-identical (6/6 renders).
Auto-fit S(phi_e) cos(phi_e) keeps the horizontal FOV with no border. The math lives in
`servers/rendering/panini_projection.h`, shared with Camera3D, whose `unproject_position`, `project_ray_normal`,
`project_local_ray_normal` and `project_position` follow the projection (camera attributes, else the World3D's).
Renderer input: `CameraEffectsContext::projection_tan_half` (single perspective view only; orthogonal and XR skip it).
- Verified: dots within 0.39 px of an independent numpy model in Forward+, Mobile, Compatibility and WebGPU; Camera3D
  unproject vs model 6e-6 px, round trips <= 2.5e-4 px; verticals straight (<= 0.39 px); a horizontal rod within
  0.47 px of the model's curve; sphere shapes within 0.03 of the model (round at d = 1, s = 1, vs 1.79 rectilinear).
- Not tested: MSAA, scaled 3D, frustum offset, Camera3D with distortion (not followed, documented).
- Follow-up: a Forward+ matrix arm `panini` (d = 1 at 120 degrees).

## Trap (if the lead agrees it is worth one)

- A `SceneTree` `-s` script's `root.size` in `_initialize()` is not the window's yet (64x64 headless, square under
  xvfb): a camera FOV computed from it is wrong, and the run still "works". Pass the size in, or compute in the first
  frame. Cost me one GPU run.

## Questions for the user

- **`panini-vertical-compression`** (choice): what `panini_vertical_compression` means. **Default (built): 1 = the
  paper's projection (things on the horizon keep their shape, horizontal lines bow toward the horizon), 0 = the
  rectilinear vertical (horizontal lines straight, things at the sides a little tall).** Alternative: Unreal's
  `r.LensDistortion.Panini.S` convention (0 default, different formula).
- **`panini-fov`** (choice): the auto-fit keeps the horizontal FOV and narrows the vertical one (to 2/3 of its tangent
  at 120 degrees, d = 1). **Default (built): leave it; users raise `fov`.** Alternative: Camera3D renders a wider
  frustum while Panini is on so the vertical FOV is kept (costs resolution and changes culling/frustum).
- **`panini-look`** (look): `panini_cases.png` (off / d1 s1 / d1 s0 / d0.5 s0.5 at 120 degrees) by eye, ideally in a
  first-person scene.
