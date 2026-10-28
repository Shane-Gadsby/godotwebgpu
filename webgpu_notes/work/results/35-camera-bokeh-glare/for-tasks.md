# For TASKS.md (task 35)

## §4 entry (proposed §4.53)

### 4.53 Aperture blades, cat's eye, tilt-shift, starburst, halation; lens flare, veiling glare, diffusion filter (October 2026) *(work task 35, camera batch)*

- **CameraAttributes:** `aperture_*` (enabled, blade count, curvature, rotation) replaces the project's bokeh shape per
  camera; `cats_eye_*` (the barrel's clipping, the same pupil/barrel model as task 33's optical vignetting);
  `starburst_*` (diffraction spikes from the aperture's straight edges, line density lambda f l / (4 pi^2 A x^2), light
  moved out of the source, at most 90%); `halation_*` (0.112 mm Gaussian at radius 1, red 1 / green 0.3 / blue 0, light
  moved). **CameraAttributesPhysical:** `tilt_shift_*` (Scheimpflug hinge rule; per-pixel focus distance in the DOF).
  **Environment:** `lens_flare_*` (8 ghosts at magnifications -0.55, -1.1, -1.7, 0.45, -0.3, 1.35, -2.3, 0.75 and a
  halo), `veiling_glare_*` (2% of the frame mean at 1), `diffusion_filter_*` (grade x 15%, black: x 7.5% and tighter).
  RenderingServer: `camera_attributes_set_aperture/_cats_eye/_tilt_shift/_starburst/_halation`,
  `environment_set_lens_flare/_veiling_glare/_diffusion_filter`.
- **Bokeh:** shape and tilt in the bokeh push constant (now exactly 128 bytes, static_assert): a custom shape forces
  the circular gather, whose scatter-as-gather test scales each sample's reach by `bokeh_shape_extent()`
  (bokeh_dof_inc.glsl); tilt replaces the focus distance and CoC scale per pixel in the blur-size pass.
- **Glare chain:** `_render_camera_glare_chain()` builds an unthresholded RGBA16F mip chain of the frame with glow's
  passes (Forward+ compute at half resolution, raster gather at quarter), bound to the tonemap pass (Forward+ set 2
  binding 2, Mobile set 0 binding 6, anisotropic sampler); parameters in the CameraEffects glare region
  (`_fill_effects_glare()`, `camera_glare_inc.glsl`), header.y bits. `CameraEffectsContext` gained the Environment's
  glare settings and the chain's size, mip count and measured blur (`glare_chain_sigma`: Forward+ 2.68, raster 0.80
  lod-0 texels per 2^lod). `camera_attributes_uses_effects_buffer()` takes the Environment's glare too (Mobile skips
  the subpass). Glare is applied before vignetting.
- **Verified:** defaults pixel-identical 9/9; vertex angles, cat's eye axis ratios, tilt band, starburst counts and
  f-number scaling, ghost positions, halation size, veiling glare and diffusion energy against independent models (see
  SUMMARY.md); WebGPU repeats the native numbers.
- **Not supported:** Compatibility (no DOF; its post-process sees only display-referred light).
- **Interface reference:** the glare chain binding in the six tonemap fragment variants only.

## Findings for §2 / §3

- **Mobile's depth of field ignores CameraAttributesPhysical** (upstream, not task 35): `BokehDOF::bokeh_dof_raster()`
  never sets `use_physical_near/far` or `blur_size_near/far`, so the raster shader takes the non-physical branch with
  `smoothstep(begin, begin - 1, depth)`, and a physical camera on Mobile renders no DOF at all. Fixing it changes Mobile
  renders of physical cameras (they would gain their DOF), so it was left alone; tilt-shift sets the physical model
  itself. Candidate §2 item.
- **Mobile clips highlights** (RGB10A2 internal buffer, about 2x white), so its glare from very bright sources is
  roughly 20x weaker than Forward+ in the test scene (halation energy). Documented in the class reference.
- Trap worth noting (cost an hour): glow's raster first pass (`MODE_GLOW_GATHER`) always reduces by four; a destination
  of any other size silently reads only part of the source.

## Questions for the user

1. **starburst-default-strength** — Strength 1 is the physical amount, which game lights do not show (they are far
   dimmer than the sun relative to the scene). Keep 1 as the default (physical, honest, invisible on most scenes), or
   default to 20? **Default: keep 1**, the docs say game lights want 10-50.
2. **halation-min-size** — Forward+'s glare chain cannot draw a halo narrower than ~1% of the frame height (physical at
   radius 1 is ~0.5%). Acceptable, or worth a sharper first level (a full-resolution pass, more cost)? **Default:
   acceptable**.
3. **look** — the images in `images/` (bokeh shapes, cat's eye, tilt-shift band, starbursts, flare, halation/diffusion/
   veiling, WebGPU vs native) for a by-eye check. A cameraSim-style scene with these on was not judged.
