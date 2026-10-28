# For TASKS.md (task 45, Environment color grading)

## §4 entry (new, after the post-processing survey's items)

### 4.x Environment color grading (work task 45, 2026-10-28)

SURVEY.md entry 1 (`pp-grading`), built as recommended. New **Color Grading** group on `Environment` (`grading_*`,
28 properties): color filter, hue shift, vibrance; lift/gamma/gain; shadows/midtones/highlights with ranges; channel
mixer; split toning; curves (master, R, G, B, hue-vs-hue, hue-vs-sat, sat-vs-sat, lum-vs-sat; `Curve` resources,
sampled 256 times by the scene). RS: `environment_set_color_grading()`, `_shadows_midtones_highlights()`,
`_channel_mixer()`, `_split_toning()`, `_curves()`.

- **Model** (`servers/rendering/storage/color_grading.cpp`, `RendererColorGrading`): display-referred, on the
  tonemapped sRGB-encoded image after brightness/contrast/saturation, in URP's LutBuilderLdr order (filter, mixer and
  SMH in linear light; LGG, split toning, hue/sat curves, vibrance and YRGB curves on encoded values). Baked on the CPU
  into a 33^3 RGBA16F LUT of **lattice points** only when a grading setter bumps the environment's
  `color_grading_version`.
- **Forward+ / Mobile / web**: the LUT goes through the existing color correction slot (no new binding in the tonemap
  shaders, interface unchanged). `RendererRD::ColorGrading` (effects/color_grading.*) keeps per environment the CPU
  bake and up to two finished LUTs (sRGB-out and linear-out for HDR viewports); a 2-variant compute shader
  (`effects/color_grading.glsl`) composes grade -> user `adjustment_color_correction` LUT (1D or 3D, sampled exactly as
  the tonemap would) and runs only when the version or the user LUT changes. It runs from `render_scene()` before any
  render pass (Mobile's subpass tonemap cannot dispatch). The tonemap samples it at lattice points through a new
  Forward+ push-constant flag bit 24 (`FLAG_COLOR_CORRECTION_LATTICE`; `CAMERA_LUT_PARAMS` now masks bits 8-23) and,
  in Mobile, the former `pad[0]` (`color_correction_lattice`). User LUTs keep their old texel-center sampling.
  Grading with adjustments off turns BCS on with neutral values.
- **Compatibility**: post pass specialization `USE_COLOR_GRADING`, the grading LUT at texunit 5 applied before the
  user LUT (two lookups; no compose needed), uploaded with glTexImage3D when the version changes; grading forces the
  post pass.
- Precompile list: `color_grading.glsl` user_lut_3d / user_lut_1d (294 modules). Interface and scan references
  regenerated: only the two new modules.
- Verification: see results/45-pp-color-grading/SUMMARY.md.

## Notes for §3 (traps), if wanted

- Godot's Environment color correction LUT is sampled with plain coordinates (texel centers at (i+0.5)/N, edges
  clamped), unlike the camera LUT. A LUT of lattice points through that path lifts black to the first texel's value
  (1.5% at 33^3). The grading LUT therefore carries a flag that remaps to lattice points; anything else that wants to
  reuse the slot must do the same.

## Questions for the user

- `grading-hdr` (choice, default "Later"): an HDR (pre-tonemap, log-domain) grading mode, as URP's "High Dynamic
  Range" grading mode — Forward+ only, needs set 3 binding 2. **Later** (default; SURVEY says only after the LDR
  version) / Build it now.
- `grading-look` (look): `results/45-pp-color-grading/grid_forward_plus.jpg` (ungraded, teal-orange, bleach, warm) and
  `teal_orange_renderers.jpg` (Forward+, Mobile, Compatibility). Do the controls give the looks you expect?
