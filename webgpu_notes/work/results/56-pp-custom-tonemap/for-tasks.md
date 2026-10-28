# For TASKS.md (task 56, custom tonemap curve)

## §4 entry

### 4.xx Custom tonemapper and the EnvironmentEffects buffer (October 2026) *(work task 56; survey §2.48, `pp-custom-tonemap`)*

`Environment.tonemap_mode = TONE_MAPPER_CUSTOM` (5) with `tonemap_custom_slope/toe/shoulder/black_clip/white_clip`
(defaults 0.88/0.55/0.26/0/0.04), RS `environment_set_tonemap_custom()`, `ENV_TONE_MAPPER_CUSTOM`. The curve has the
parameters of Unreal Engine's Film tonemapper and its defaults: in log10(x) a straight line of slope `slope`, joined in
value and slope to a tanh toe (takes over below output 1 - toe, falls to -black_clip) and a tanh shoulder (takes over
above output `shoulder`, rises to 1 + white_clip); 0.18 -> 0.18; where the shoulder starts before the toe ends (the
defaults) the two are blended with a smoothstep over the overlap; output clipped to [0, 1] (SDR curve, like Filmic/ACES),
per channel in linear Rec.709. Model and derivation: `RendererEnvironmentStorage::tonemap_custom_compute_curve()`
(constants converted to log2 on the CPU, 12 floats); evaluated analytically in `tonemap_custom_inc.glsl` (no LUT: exact,
~3 tanh per channel, only when selected). Its `white` (for glow's Screen blend) is where the curve reaches 254.5/255.
- **EnvironmentEffects uniform buffer** (`environment_effects_inc.glsl`, `RendererEnvironmentStorage::EnvironmentEffectsUBO`):
  the first Environment-owned tonemap parameter buffer, std140, 256 bytes: `uvec4 header` (x: tonemap bits, bit 0 Custom;
  y, z, w free), `vec4 tonemap_custom[3]`, `vec4 reserved[12]` (free: an HDR grading mode would go here). Filled by
  `environment_fill_effects_buffer()` (its own `/* Custom tonemap */` block; returns false when nothing in it is used,
  so an all-zero shared buffer is bound). Forward+ tonemap **set 3 binding 2** (WGSL binding 4), Mobile set 0
  **binding 7** (WGSL 14) plus spec constant 19 `tonemapper_custom` (ADRENO_BUG moved to bit 20), Compatibility: the
  12 floats appended to `TonemapData` (scene/sky/post tonemap, `drivers/gles3/shaders/tonemap_custom_inc.glsl`).
  Per viewport (`EnvironmentEffectsBufferRD`, uploaded only on change); `_update_environment_effects()` fills it before
  the render pass, so the Mobile subpass tonemap (which cannot update buffers) reads it with `p_update = false`.
- Forward+ matrix arm `tonemap_custom` (every parameter off its default, against the fixture's Filmic).
- Verified: <numbers from SUMMARY.md>.

## §3 trap candidates

- **The gles3 shader includes are tracked by directory.** `drivers/gles3/shaders/SCsub` makes the shaders in that
  folder depend on `*_inc.glsl` in the same folder only; an include in `effects/` used by `scene.glsl` would not
  rebuild `scene.glsl.gen.h` when edited. The Custom curve's Compatibility copy therefore lives in
  `drivers/gles3/shaders/tonemap_custom_inc.glsl`, not under `effects/` as camera-batch.md suggests for post-pass code.
- **A buffer the Mobile subpass tonemap reads must be uploaded before the render pass begins**
  (`_update_environment_effects()` next to `_update_color_grading()`); inside the pass `buffer_update()` is an error.
  The CameraEffects buffer side-steps this by disabling the subpass; an always-on parameter buffer should not.
- (Only if it bit us during verification: see SUMMARY.md.)

## Questions for the user

- `custom-tonemap-gamut` (choice). Where the Custom curve is applied. **Default: per channel in linear Rec.709**, like
  Filmic, ACES (fitted) and Reinhard here; simple, and the readback matches the 1D curve exactly. Alternative: as
  Unreal does, in ACEScg (AP1) primaries with its fixed pre/post desaturation (0.96/0.93), which shifts saturated
  highlights' hue less but means the curve no longer equals what a gray card shows per channel, and would not match
  values copied from Unreal unless the desaturation came too.
- `custom-tonemap-hdr` (choice). HDR output (HDR 2D / `Window.hdr_output_requested`): **Default: clip at 1.0** (an SDR
  curve, like Filmic and ACES). Alternative: let the shoulder use the output's max value as its asymptote (like AgX),
  which needs a rule for how `white_clip` scales with the headroom.
