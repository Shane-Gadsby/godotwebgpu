# Task 56: Custom tonemap curve (`pp-custom-tonemap`) — summary

Branch `work/56` (worktree `godotwebgpu-56`), on `webgpu-4.8` at `6028459941`.

## What changed

- **API**: `Environment.TONE_MAPPER_CUSTOM` (5) and `tonemap_custom_slope` 0.88, `_toe` 0.55, `_shoulder` 0.26,
  `_black_clip` 0.0, `_white_clip` 0.04 (shown only for Custom; `tonemap_white` hidden for Custom);
  `RenderingServer.environment_set_tonemap_custom(env, slope, toe, shoulder, black_clip, white_clip)`,
  `ENV_TONE_MAPPER_CUSTOM`. Class docs written (`--doctool` checked; it also touched `CameraAttributes.xml` with Panini
  text that is not mine, restored).
- **Model** (`RendererEnvironmentStorage::tonemap_custom_compute_curve()`, `environment_storage.cpp`): Unreal Engine's
  Film tonemapper parameters and defaults, re-derived (no Unreal code): in log10(x) a line of slope `slope`, a tanh toe
  that takes over below output 1 - toe (asymptote -black_clip) and a tanh shoulder above output `shoulder` (asymptote
  1 + white_clip), matched in value and slope; 0.18 -> 0.18 (on the toe when toe < 0.82, else on the line; Unreal
  switches at 0.8, which misses the anchor for toe in (0.8, 0.82)); overlapping toe and shoulder (the defaults) blend
  with a smoothstep; clipped to [0, 1]. Chosen over a baked log2 LUT because it is exact, cheap (2 tanh + 1 log2 per
  channel, only when selected) and needs no texture slot; over Hable's piecewise power curve because the five
  requested controls are Unreal's, which map 1:1. The defaults equal Unreal's default (ACES-matching) curve:
  0.18 -> 0.18, 1 -> 0.723, 4 -> 0.944, white (254.5/255) from 9.6.
- **EnvironmentEffects UBO** (new, `environment_effects_inc.glsl` / `RendererEnvironmentStorage::EnvironmentEffectsUBO`,
  std140 256 B: `uvec4 header` (x tonemap bits; y, z, w free), `vec4 tonemap_custom[3]`, `vec4 reserved[12]`),
  filled by `environment_fill_effects_buffer()`; all-zero shared buffer when unused. Forward+ tonemap set 3
  binding 2; Mobile set 0 binding 7 + spec constant 19 `tonemapper_custom` (ADRENO_BUG moved to bit 20); Compatibility:
  12 floats appended to `TonemapData`. Per viewport `EnvironmentEffectsBufferRD`, uploaded on change only;
  `_update_environment_effects()` fills it before the render pass so Mobile's subpass tonemap reads it unchanged.
- Shader: `tonemap_custom_inc.glsl` (RD) and `drivers/gles3/shaders/tonemap_custom_inc.glsl` (same text).
- **Fix found on the way**: `drivers/gles3/shaders/effects/SCsub` now makes the effects shaders depend on the parent's
  `*_inc.glsl` (post.glsl kept a stale tonemap_inc.glsl; see for-tasks.md §3).
- Forward+ matrix arm `tonemap_custom` (all five parameters off default vs the fixture's Filmic), minDelta 9.8.
- Interface/scan references regenerated (only change: the UBO at WGSL (3,4) in the 4 tonemap fragment variants and
  (0,14) + override 19 in the 2 tonemap_mobile fragment variants).

## Verification (numbers in numbers.txt, plot custom_curve_parameters.png, tools/ reproduce everything)

1. **Curve vs an independent numpy model** (`tools/curve_ref.py`, float64, written from the definition): a 16x16
   chart of unshaded patches spanning 2^-12..2^6 (768 channel samples), 13 cases (defaults, each parameter low/high,
   toe 0.9 for the line-anchored branch, a mix, exposure 2):
   - Forward+: max 0.64/255, mean 0.09-0.26, **0 samples over 1/255** in every case.
   - Web (Chrome, Vulkan, nvidia/lovelace, 48 textures; baked export): identical numbers; **pixel-identical to
     native** on all 13 chart cases, max 1/255 (mean 0.0002) on the lit scenes.
   - Mobile: 97.9-100% of samples within 1/255, max 1.4-5.4 — the same harness gives Linear max 6.7 and Filmic 5.3
     (the RGB10A2 input, x/2 in 10 bits, is coarse in the darks). Its 2.0 cap: the shoulder sees nothing brighter,
     max output 0.86 with the defaults (documented).
   - Compatibility: its chart readback does not follow any model for **any** tonemapper (Linear max 74, Filmic 81,
     identical in the pre-change editor; task 45 found the same), so checked by consistency: post-pass vs scene-shader
     tonemap of the same chart within 3/255 (Filmic: 6/255); lit scene vs Forward+ mean 7.7/255 (Filmic 4.7, AgX 7.0).
2. **Each parameter** (plot, model lines with Forward+ readback dots): slope steepens around a fixed 0.18; toe darkens
   and compresses shadows; shoulder raises/brightens and clips highlights sooner; black clip crushes to 0 at a finite
   input; white clip reaches 1.0 at a finite input (white0 never does: 0.98 at 64).
3. **Existing tonemappers bit-identical**: 42 renders (Linear/Reinhard/Filmic/ACES/AgX x chart and lit scene with
   glow x 3 renderers, plus 2 Compatibility post-pass charts): **0 px different** from the pre-change editor.
4. WebGPU vs native: see 1. smoke.sh (last lines):
   ```
   [WGSL Precompile] Results: 309 compiled, 0 glsl failures, 0 conversion failures
   identical interface: 309/309, mismatched: 0, missing in candidate: 0, not in reference: 0
   against the stored reference: 618/618 identical, 0 differ, 0 missing, 0 not in reference
   validation: 309 modules, 0 invalid, 0 of them expected
   SMOKE OK (smoke-20261028-154658-2180688)
   ```
5. **Matrix** `run_forward_plus.mjs --baked --parity --feature tonemap_custom,tonemap,tonemap_exposure` (BAKE=1
   export from this tree's editor and debug template): tonemap_custom PASS delta 29.602, native diff off 0.002 on 0.002;
   tonemap and tonemap_exposure PASS.
- Editor built with `dev_mode=yes` (werror): clean.

## What the lead must still run

- Regenerate the interface/scan references after merging with task 57 (both add tonemap bindings).
- The matrix arm on the merged tree with `--parity`; the bit-identical check on the merged tree.

## Risks / notes

- Task 57 also edits `tonemap.glsl`, `tone_mapper.{h,cpp}`, `renderer_scene_render_rd.cpp`: my changes are their own
  blocks (`EnvironmentEffects`, `/* Environment effects */`, `/* Custom tonemap */`); Mobile spec bit 19 is taken.
- The curve's overlap blend follows Unreal's documented behavior (the defaults depend on it); the code is my own
  derivation, no Unreal source.
- Per channel in linear Rec.709, clipped at 1.0 (questions `custom-tonemap-gamut`, `custom-tonemap-hdr` in
  for-tasks.md, built with the defaults).
- Not tested: XR/multiview, HDR 2D output, scaled 3D viewports, Android.
- Lease note: one batch of 3 small Compatibility debug renders (llvmpipe, software GL, no GPU use) ran while
  worker-55 held the `gpu` lease, before I noticed; everything else ran under my lease.
