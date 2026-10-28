# For TASKS.md and README (task 33: lens optics)

## TASKS.md §4 entry (new subsection after §4.48 camera LUT)

### 4.49 CameraAttributes lens optics and the CameraEffects uniform buffer (2026-10-28, work task 33)

Sensor format, vignetting, lens distortion, lateral chromatic aberration, anamorphic lenses, ND filters and T-stops,
diffraction softening and focus breathing, in Forward+, Mobile, Compatibility and WebGPU Forward+.

- **CameraEffects uniform buffer** (`camera_effects_inc.glsl`, `CameraEffectsUBO` in `camera_attributes_storage.h`):
  std140, 512 bytes: `uvec4 header` (x lens bits, y glare bits, z exposure-automation bits, w free), `vec4 lens[8]`,
  `vec4 glare[6]` (task 35), `vec4 exposure[2]` (task 36), `vec4 reserved[15]`. Forward+ tonemap set 1 binding 1
  (next to the exposure texture; set 3 is shared with push-constant emulation on WebGPU), Mobile tonemap set 0 binding
  5, Compatibility post pass UBO binding 16 behind the `USE_CAMERA_EFFECTS` specialization. Filled per tonemap pass by
  `RendererCameraAttributes::camera_attributes_fill_effects_buffer()` (one entry point calling `_fill_effects_lens/
  _glare/_exposure`; the last two are stubs), owned per viewport (`CameraEffectsBufferRD`, render-buffers custom data)
  and uploaded only when its contents change; with no effect on, the passes bind a shared all-zero buffer, and every
  effect sits behind a uniform branch on its header word, so existing projects are bit-identical. Mobile skips the
  subpass post-process when the buffer is in use (`camera_attributes_uses_effects_buffer()`).
- Order in the pass: lens resample (distortion, CA, diffraction) at the very first read, FXAA following the lens (its
  final read is redone through the lens when CA or diffraction is on), glow read where the lens maps the pixel,
  vignetting after the pre-tonemap glow, then white balance, grain, tonemapping, LUT.
- Models and sources are in the comment block above `_fill_effects_lens()`.
- Interface references regenerated: only the new uniform buffer in the four tonemap and two tonemap_mobile fragment
  variants.

## README engine additions line

- **Camera lens optics** (CameraAttributes): sensor formats, physical vignetting (cos⁴ + optical), Brown-Conrady
  distortion (automatic per focal length, auto-crop), lateral chromatic aberration, anamorphic squeeze, ND filters and
  T-stops, Airy diffraction softening and focus breathing, in every renderer and on the web.

## §2 follow-ups (not done here)

- A Forward+ matrix arm (`lens_optics`) with `--parity`: the effects are checked numerically here, but no matrix arm
  was added (the matrix's fixture renders at a fixed size; a vignetting + distortion + CA arm would catch regressions).
- Longitudinal chromatic aberration (purple fringing on out-of-focus edges, which does shrink when stopping down) would
  belong in the DOF pass, not the tonemap pass.

## Questions for the user

1. **Focus breathing default** (`lens_focus_breathing`). The task asked for "default a typical stills lens", but the
   batch rule is that a new effect is off by default and existing projects render bit-identically; breathing changes
   the field of view of every existing CameraAttributesPhysical camera whose focus distance is close.
   - **0.0, no breathing (default, built)**: existing projects unchanged; set 1.0 for a unit-focusing stills lens.
   - 1.0, a unit-focusing stills lens: realistic out of the box, but existing physical cameras focused close get a
     narrower field of view.
2. **Chromatic aberration and the aperture.** The task asked for CA "stronger wide open". Lateral CA physically does not
   change with the aperture (it is a chief-ray aberration), so it does not here; what does shrink when stopping down is
   longitudinal CA (out-of-focus color fringing).
   - **Physical: lateral only, aperture-independent (default, built)**; longitudinal CA as a later DOF-pass task.
   - Fake an aperture dependence on the lateral fringe for the look.
