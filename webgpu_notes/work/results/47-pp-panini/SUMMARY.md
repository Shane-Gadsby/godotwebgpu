# Task 47: Panini projection: SUMMARY

Branch `work/47` on 7b978e6baa. Worker-47, 2026-10-28.

## What changed

- **CameraAttributes** (both kinds): `panini_enabled` (false), `panini_distance` d (1.0, range 0-1),
  `panini_vertical_compression` s (1.0, range 0-1); own inspector group "Panini Projection". RenderingServer
  `camera_attributes_set_panini(rid, enable, distance, vertical_compression)`; storage `/* Panini projection */` block
  at the end of `camera_attributes_storage.cpp`.
- **Model** (`servers/rendering/panini_projection.h`, new, header-only, shared by storage and Camera3D): Sharpless,
  Ballestrem and Thomas 2010. Direction -> unit cylinder (azimuth phi, height h) -> viewed from d behind the axis:
  X = S sin(phi), Y = S h, S = (d+1)/(d+cos phi). d = 0 is rectilinear. Vertical compression blends Y from the
  rectilinear y (s = 0: horizontal lines straight) to the paper's S h (s = 1: conformal on the horizon); no effect at
  d = 0. **This is my definition of "vertical compression"** (see questions): Unreal's S formula did not give a
  consistent meaning in the direction the image is resampled, so I used the one that is exactly the published
  projection at 1 and exactly straight horizontals at 0.
- **Auto-fit, no border**: output scale S(phi_e) cos(phi_e) (phi_e = horizontal half FOV). Proven in the header and
  checked numerically: the frustum's side edges stay vertical, its top edge bows monotonically, so this scale puts the
  output corners on the image's corners for any s in [0, 1]. The horizontal FOV is kept; the vertical one narrows
  (0.667 of its tangent at 120 degrees, d = 1). An independent bisection on the frustum border image agrees to 1e-12.
- **Shader**: `camera_lens_inc.glsl` (RD + Compatibility copy) gets `CAMERA_LENS_PANINI` (header.x bit 32, in
  `CAMERA_LENS_RESAMPLE`) and `camera_lens_panini_source()`, applied in `camera_lens_source_uv()` after lateral color
  and undistortion (order lens -> projection). Parameters in the free lens slots, no layout change:
  `lens[2].zw` = output half extent in the Panini plane, `lens[4].zw` = 1 / rendered tangent half extent,
  `lens[7].zw` = d, s. Inverse in closed form: phi = atan(k) + asin(k d / sqrt(1+k^2)), k = X/(d+1).
  `_lens_requested_bits()` includes it (so `camera_attributes_uses_effects_buffer()` is right: Mobile drops the
  subpass, Compatibility forces the post pass); `_fill_effects_panini()` drops the bit without a single perspective
  view.
- **Renderer input**: `CameraEffectsContext::projection_tan_half` (1/P00, 1/P11), filled in
  `renderer_scene_render_rd.cpp` (Forward+, Mobile) and `rasterizer_scene_gles3.cpp` (Compatibility) only for a
  non-orthogonal, single-view camera. XR and orthogonal: off (documented).
- **Camera3D**: `unproject_position` / `safe_unproject_position`, `project_ray_normal` / `project_local_ray_normal`,
  `project_position` apply the same mapping (`_get_panini_mapping()`: the camera's attributes, else the World3D's;
  not orthogonal). `project_ray_origin` is unchanged (the camera position for perspective; verified 0 m).
  Directions past 90 degrees off-axis come from the cylinder form, so off-screen points still give valid rays.
- Class docs: CameraAttributes (3 members, inserted by hand: `--doctool` re-sorts the whole file, which the merges
  left unsorted; my entries are byte-identical to doctool's), RenderingServer method, notes on the four Camera3D
  methods.

## Verification (numbers in `numbers.txt`, images `panini_cases.png`, scripts in `tools/`)

Scene: camera with a 120 degree horizontal FOV, 1600x900; green dots on a 10 m sphere every 8 degrees of azimuth x 10
of elevation, red vertical poles, a blue horizontal rod 2.5 m below and 6 m ahead, white spheres of 2 degrees radius.
The reference (`tools/analyze.py`) is written from the paper's cylinder geometry in numpy, independently of the
engine's code, with the fit found by bisection. Mesh LOD off (it coarsened the dots in Forward+/Mobile only).

- **Bit-identical default**: base editor (7b978e6baa) vs work/47, sky + sun shadows + glow + Filmic, 1280x720:
  attributes untouched and Panini enabled with d = 0: **max diff 0, 0 pixels, in all 6 renders** (Forward+, Mobile,
  Compatibility).
- **Positions** (dot centroids vs the reference, 81-111 dots per case, dots under the rod skipped), all three
  renderers: off max 0.27 px; d1 s1 0.29 (Compat 0.37); d1 s0 0.34; d0.5 s0.5 0.37 (Compat 0.39). Mean 0.12-0.19 px.
- **Camera3D follows the image**: `unproject_position()` of each dot vs its rendered centroid: the same numbers (max
  0.39 px); `unproject_position()` vs the reference: **max 6e-6 px** (headless, 135 dots, 3 cases).
- **Round trips** over a 33x19 screen grid incl. edges: ray -> point -> unproject max 2.5e-4 px, `project_position`
  (0.5 m, 20 m) -> unproject max 1.4e-4 px, `project_position` on the `project_ray_normal` ray within 1.5e-7 rad,
  `project_ray_origin` = camera position (0 m).
- **Straight verticals**: 10 pole segments, per-row centroid peak-to-peak <= 0.39 px over ~300-900 rows (Compat 0.00).
- **Horizontal lines vs the formula**: the rod's per-column centroid vs the reference curve: max 0.47 px; it bows
  94 px at d1 s1 and 31.5 px at d0.5 s0.5, and stays straight at s = 0 (0.4 px).
- **Edge stretch** (white spheres, width/height from second moments vs the reference silhouette filled): rectilinear
  at 56 deg 1.764 measured vs 1.769 expected (1/cos 1.788); d1 s1: 0.979-1.000 vs 0.995 at every azimuth (round, as
  the paper's conformality on the horizon predicts); d1 s0 at 56 deg 0.705 vs 0.708; d0.5 s0.5 1.049 vs 1.058.
- **WebGPU** (system Chrome headed, Vulkan, adapter nvidia/lovelace, not fallback, 48 textures; log "WebGPU 1.0 -
  Forward+"), 1280x720, off / d1s1 / d0.5s0.5, analyzed against the reference: dots max 0.26-0.39 px, rod 0.24-0.33
  px, shapes within 0.03, poles ptp <= 0.34 px: the same agreement as native.
  **Not pixel-identical to native, and not because of Panini**: the web capture has partial-coverage values at
  geometry edges (e.g. 96 next to a 255 pole) where native has hard edges, already with Panini **off** (1.1% of
  pixels; 3.3-3.5% with Panini, which magnifies edges 1.5x). Task 33 got pixel-identical captures, so something in
  my setup (a sub-pixel canvas offset in Playwright's element screenshot is my guess) differs; I did not chase it.
- **Smoke** (final code):
  ```
  identical interface: 292/292, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 584/584 identical, 0 differ, 0 missing, 0 not in reference
  validation: 292 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261028-123039-1080523)
  ```
  No interface reference changes (no new binding).
- **Builds**: editor with the build-linux.sh flags, then **`dev_mode=yes` (werror): clean**; web `template_debug`
  (no dlink, no threads). `pre-commit` clean on all changed files.

## What the lead must run

- After merging with 45/46/48/49: smoke (no reference change expected from this branch), and a quick
  `tools/run2.sh` + `tools/summarize.py` on the merged editor (`tools/` paths point at my scratch dir; adjust `W`).
- Optional: the web-vs-native edge difference above with a known-good capture method (task 33's `shoot.mjs`).

## Risks

- Conflicts: `camera_attributes_storage.{h,cpp}` (one line in `_lens_requested_bits()`, three in `_fill_effects_lens()`
  before `header[0]`, a struct field and a context field), `camera_lens_inc.glsl` (doc lines for lens[2]/[4]/[7], the
  RESAMPLE define, `camera_lens_source_uv()`), the RS files (one line each, after task 44's longitudinal CA),
  `CameraAttributes.xml` (inserted after `noise_reduction_strength`). Anything else using `lens[2].zw`, `lens[4].zw`
  or `lens[7].zw` collides; none of 45-50 did when I started.
- The center is magnified by 1/fit (1.5x at 120 degrees, d = 1): softer center, as in Unity/Unreal.
- Not tested: MSAA, XR (deliberately off), scaled 3D resolution (FSR), frustum cameras with an offset (projected around
  the frame center, consistently in shader and Camera3D), combined with distortion (the shader composes them;
  Camera3D follows only the Panini, documented).
- `Camera3D.get_frustum()` / `is_position_in_frustum()` keep the rendered frustum (a superset of what shows).
