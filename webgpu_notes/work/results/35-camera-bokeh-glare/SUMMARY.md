# Task 35: aperture blades, cat's eye, tilt-shift, starburst, halation; lens flare, veiling glare, diffusion filter

Branch `work/35` on webgpu-4.8 at a51fa557fb. Worker-35, 2026-10-28.

## What changed

**CameraAttributes** (both kinds; RenderingServer `camera_attributes_set_aperture/_cats_eye/_starburst/_halation`):
- `aperture_enabled/_blade_count/_blade_curvature/_rotation`: the camera's diaphragm replaces
  `rendering/camera/depth_of_field/depth_of_field_bokeh_shape` for its DOF (0 = round; curvature mixes the polygon's
  radius with the circle's; corners at rotation + k 360/N, counterclockwise on screen). An anamorphic squeeze (task 33)
  makes any bokeh an oval taller than wide by the squeeze, even with the aperture off (only reachable with squeeze > 1).
- `cats_eye_enabled/_strength`: bokeh = aperture ∩ barrel circle, task 33's optical-vignetting geometry (barrel radius
  N / max f, offset 0.5 frame/sensor radius x N / max f per frame unit, toward the center): lemon-shaped wide open,
  round two stops down. Practical: a lens wide open.
- `starburst_enabled/_strength`: diffraction spikes, N for even blade counts, 2N for odd, perpendicular to the edges.
  Line density lambda f l / (4 pi^2 A x^2) of the source's light per unit length (Born & Wolf 8.3/8.5, Goodman 4.2),
  lambda 550 nm, polygon inscribed in the f / N circle, x (1 - curvature). The spike light is taken from the source
  (energy-preserving, capped at 90%). Uses the aperture group's blades whether or not the aperture shapes the bokeh.
- `halation_enabled/_strength/_radius`: red 1 / green 0.3 / blue 0 halo, Gaussian sigma 0.112 mm x radius on the
  sensor (2 t tan(41.8°) / 2 for a 0.125 mm base), 8% of the light at strength 1, moved rather than added.

**CameraAttributesPhysical**: `tilt_shift_enabled/_tilt/_axis` (RS `camera_attributes_set_tilt_shift`). Hinge rule
(Merklinger): plane normal ∝ s sin(theta) b + f z; the DOF's blur-size pass computes each pixel's focus distance where
its view ray meets the plane and rescales the physical CoC by (s - f) / (s' - f). DOF runs whenever tilt is on.

**Environment** (RS `environment_set_lens_flare/_veiling_glare/_diffusion_filter`): `lens_flare_*` (intensity 1 =
0.1% per ghost, threshold in exposed units, up to 8 ghosts at fixed magnifications, halo, per-color ghost size),
`veiling_glare_*` (2% of the frame's mean at 1, ISO 9358's typical range), `diffusion_filter_*` (grade x 15% scattered
into 1-8% of frame height, black: x 7.5% into 0.5-4%), all energy-preserving except the flare.

**Renderer plumbing:**
- Bokeh push constant grows from 96 to 128 bytes (static_assert); `BokehDOF::_setup_bokeh_shape()` fills shape, cat's
  eye, squeeze, view-ray and tilt-plane fields (half floats packed in pairs). A custom shape forces the circular gather,
  whose test becomes `smoothstep(r - 0.5, r + 0.5, coc * bokeh_shape_extent(dir, uv, near))`. Defaults: `shape_flags`
  0, every new path behind it.
- Glare chain: `_render_camera_glare_chain()` (renderer_scene_render_rd.cpp), an RGBA16F mip chain down to 1x1 with no
  threshold, built with glow's own passes (Forward+ `gaussian_glow` at half resolution; raster
  `gaussian_glow_downsample_raster` at quarter, as its gather always reduces by 4). Built only when glare is on.
- Tonemap binding: Forward+ set 2 binding 2, Mobile set 0 binding 6 (not in the subpass variant), with an anisotropic
  (16x) trilinear sampler owned by ToneMapper; a black texture otherwise.
- CameraEffects glare region filled by `_fill_effects_glare()` (models and sources in its comments), read by
  `camera_glare_inc.glsl` behind `header.y`. `CameraEffectsContext` gains `glare_environment`, `glare_chain_size`,
  `glare_chain_mipmaps`, `glare_chain_sigma`. `camera_attributes_uses_effects_buffer(rid, env glare)`: Mobile skips
  its subpass when glare is on. The Environment's glare runs without CameraAttributes (fill handles a null RID).
- Class reference for everything (doctool; the other diffs in the XML are doctool's reordering).
- Interface references regenerated: only `(2, 4, sampler)`, `(2, 5, texture_2d)` in the four tonemap fragment variants
  and `(0, 12, sampler)`, `(0, 13, texture_2d)` in the two tonemap_mobile ones (bindings after combined-sampler
  splitting); scan_reference renumbered around them.

## Verification (native: xvfb, RTX 4080 SUPER, Vulkan, 1280x720, linear tonemapper, sRGB-decoded 8-bit; scripts in
`tools/`, references computed independently in `tools/analyze.py`)

- **Defaults bit-identical**: base editor (a51fa557fb, built in this worktree) vs work/35, scene with sky, shadows,
  glow, Filmic; Physical / Practical / none x Forward+ / Mobile / Compatibility: **max diff 0, 0 pixels in 9/9**.
  PENDING_DEFAULTS
- **Aperture blades** (Physical 50 mm f/1.4, focus 1 m, point at 20 m; vertex angle from the phase of r(theta)'s N-th
  harmonic): 5 blades 0.19° / -3.04° from the set 0° / 20°; 6 blades -0.13° / -1.56°; 7 blades +0.29° / -0.41°.
  The dominant harmonic of r(theta) was 5 and 6 for 5 and 6 blades; for 7 the 6th harmonic of the gather's speckle
  edged out the 7th, so the phase fit is the measure there. Edge-to-corner ratio 0.77-0.86 vs cos(pi/N) 0.81-0.90
  (the source's 6 px disc rounds the corners). Round: harmonic-6 amplitude 0.06 px; curvature 0.5 sits between.
  PENDING_PRACTICAL
- **Anamorphic 2x**: bokeh height/width 1.76 after removing the source disc's variance (expected 2.00; the remaining gap
  is the gather's edge softness, which adds to both axes).
- **Cat's eye** (axis ratio of each bokeh vs the two-circle model's second moments, source variance removed; minor axis
  along the radius, |cos| >= 0.99): f/1.4: 0.897/0.908, 0.856/0.820, 0.882/0.902, 0.882/0.861, 0.819/0.797 at
  (0.45,0), (0.9,0), (0,0.85), (0.6,0.6), (-0.9,-0.85); f/2.0: 0.896/0.892, 0.871/0.848 at the two corners, the rest
  round; f/2.8: all round (0.92-0.97 against 1.00, gather speckle).
- **Tilt-shift** (ground plane, 50 mm f/1.4, sharp band's row vs the hinge-rule crossing computed in analyze.py): focus
  5 m, tilt -6°: row 339 vs 344.5 (Forward+ and Mobile); tilt -2° Mobile 325 vs 327.5. Focus 3 m, tilt -6°: Forward+
  439, Mobile 394, WebGPU 386 vs 417.5 (the per-row contrast metric is noisy over a shallow band; `images/tilt_shift.png`
  shows the band at the predicted height). Tilt +1° puts the whole ground in focus; axis 90° makes the band vertical.
- **Starburst** (ring 0.05-0.12 frame heights, peaks of the angular profile): 6 / 14 / 8 spikes for 6 / 7 / 8 blades,
  angles within 0.25-1.25° of rotation + 180°/N (+ k 360°/N, both directions for odd); rotation 15° follows.
  f/8 vs f/16 spike light **0.501** (Forward+), **0.502** (Mobile), expected 0.500 (density ∝ N).
  PENDING_STARBURST
- **Lens flare** (ghost centroids vs source position through the center): k = -1.702, -1.100, -0.550, +0.450 for the
  set -1.7, -1.1, -0.55, 0.45, off the line by <= 1.6 px; same in Mobile and WebGPU.
- **Halation** (Gaussian fit of the red halo; strength 12): radius x4 sigma 13.47 px vs model 13.44, x16 54.17 vs 53.76
  (Forward+); Mobile 11.76 / 49.66 (-12% / -8%). Radius x1 hits the chain's sharpest level: 6.8 px Forward+ (model
  3.36), 2.95 px Mobile. Green/red 0.29-0.30 (0.30), blue 0. Flat field at strength 4: change 0.00000 in R, G, B.
- **Veiling glare** (half-bright frame, mean 0.1061): strength 1 dark side 0.00212 (expected 0.00212); strength 5
  0.01096 Forward+, 0.00857 Mobile, 0.01033 WebGPU (expected 0.01061).
- **Diffusion** (same frame): bright interior unchanged (0.2122), frame mean 0.1062 vs 0.1061; dark side lifted 0.0091 /
  0.0030 / 0.0003 / 0 at 10 / 40 / 120 / 300 px from the edge (grade 1); black variant 0.0030 / 0.0006 / 0 / 0.
- **Glare chain calibration**: sigma of lod L = s 2^L lod-0 texels, from the halation fits: Forward+ 2.68, raster 0.80
  (`_render_camera_glare_chain()`).
- **Mobile highlights**: Mobile's RGB10A2 buffer clips the test point (radiance ~40) near 2, so its glare from it is
  ~20x weaker than Forward+'s (halation energy 30 vs 600); Forward+ moved 0.95 of the source's energy, as set.
- **WebGPU** (system Chrome, headed, Vulkan; adapter nvidia/lovelace, maxSampledTexturesPerShaderStage 48, engine log
  "WebGPU 1.0 - Forward+"): 10 cases vs native Forward+. Same measurements: 7 blades vertex -0.80° (native -0.41°),
  cat's eye ratios within 0.01 of native, 6 / 14 spikes within 0.71°, ghosts at k -1.700 / -1.100 / -0.550 / +0.450,
  veiling 0.01033, diffusion lifts 0.0086 / 0.0027 / 0.0003. Differences: halation sigma 11.9 px (native 13.5) and the
  spikes ~30% brighter, from Chrome's anisotropic/trilinear filtering; per pixel the frames differ at object edges
  (default scene max 142/255, mean 0.66), not in the effects. PENDING_WEB
- **Smoke** (tree at 3c8bc4100a):
  ```
  [WGSL Precompile] Results: 285 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 285/285, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 570/570 identical, 0 differ, 0 missing, 0 not in reference
  validation: 285 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261028-103110-577246)
  ```

Not verified: performance (no GPU timing; the starburst is the costly one: 16 anisotropic reads per spike line per
pixel, 48 for 6 blades, 112 for 7), MSAA, XR/multiview (code paths exist, untested), scaled viewports/FSR, a
`dev_mode=yes` build, the cameraSim project by eye.

## What the lead must run

- After merging: regenerate the interface references again (this task adds the glare chain at set 2 binding 2 /
  set 0 binding 6 of the tonemap variants), then smoke.
- `./webgpu_tests/local_ci.sh --dev-mode` (built without werror).
- Optionally the checks on the merged tree: `tools/batch*.sh <editor> ...` with `tools/glare_test.gd` in a scratch
  project (`tools/render.sh` expects it at `<scratch>/proj`), then `tools/analyze.py <outdir>`; the web pair:
  `tools/web_main.gd` in a project + `tools/shoot.mjs <export> <out>`.

## Risks

- A custom aperture uses the circular gather (half resolution, the existing spiral): grainier and ~25% smaller than the
  project's full-resolution hexagon at the same settings. That is the circular mode's existing look.
- Mobile: its DOF ignores CameraAttributesPhysical (upstream; for-tasks.md), so aperture/cat's eye need Practical blur
  there; its RGB10A2 buffer clips highlights, so its glare from bright sources is much weaker than Forward+'s.
- The glare chain costs a half-resolution RGBA16F mip chain (quarter on Mobile) and its passes whenever any glare is on.
- Starburst strength far above 1 is capped at moving 90% of a highlight's light into spikes.
- Compatibility gets none of it (documented in every member's note).
