# Task 48: GTAO as an alternative ambient occlusion method — summary

## What changed

- **Project setting `rendering/environment/ssao/method`** (enum `ASSAO,GTAO`, default `ASSAO`), applied at startup by
  `SSEffects` and on settings change by `editor_node.cpp`; RS method `environment_set_ssao_method()` with enum
  `RS::EnvironmentSSAOMethod` (`ENV_SSAO_METHOD_ASSAO`, `ENV_SSAO_METHOD_GTAO`). Plumbing in its own `/* GTAO */`
  blocks: `rendering_server{.h,.cpp,_default.h,_enums.h}`, `rendering_method.h`, `renderer_scene_cull.h` (PASS1),
  `renderer_scene_render.h` (default empty virtual: Mobile, Compatibility and dummy ignore it, so no edits there),
  `RenderForwardClustered` override.
- **New shader `shaders/effects/gtao.glsl`**: XeGTAO (Intel, MIT; Jimenez et al. 2016) ported to GLSL compute, five
  modes: depth prefilter (linear view z + 4 mips in one dispatch, XeGTAO's 16x16 tile and depth MIP filter), main pass
  (horizon search + cosine-weighted visibility integral, Hilbert/R2 spatio-temporal noise, edges packed in .y),
  denoise (XeGTAO's 3x3 edge-aware blur), final denoise (writes R8), depth-aware 2x upsample (half size).
- **`SSEffects`** (`/* GTAO */` blocks): `ssao_set_method()`, `gtao_allocate_buffers()`, `generate_gtao()`. Own
  render-buffer scope `rb_gtao` (R16F view-z chain padded to 16, two RG8 working textures); writes the **same
  `RB_SCOPE_SSAO/RB_FINAL`** R8 texture ASSAO writes, so the scene shader, light/channel affect and the SSAO debug draw
  are unchanged, and RT AO (TASKS §2.34) can slot in the same way: a third method value, its own scope, the same
  final texture.
- **Forward+ `_pre_opaque_render`**: with GTAO the shared SSAO/SSIL depth downsample only runs if SSIL needs it;
  `_process_gtao()` reads the Environment's SSAO settings like `_process_ssao()`.
- **Settings mapping** (documented in ProjectSettings.xml): radius = world radius (x XeGTAO's 1.457 multiplier),
  intensity and power act relative to the Environment defaults (2.0, 1.5 give XeGTAO's reference output; power scales
  XeGTAO's 2.2 exponent), sharpness = denoiser edge bias as in ASSAO, quality = slices x steps (Very Low 2x2, Low 3x2,
  Medium 3x3, High 9x3, Ultra 9-18 x 4 by adaptive target), half_size = half resolution per axis, blur_passes =
  denoise passes, fadeout_from/to as ASSAO. detail/horizon are ASSAO-only.
  XeGTAO's own Low (1x2) and Medium (2x2) were measured and rejected as Godot's two lowest levels: without TAA they
  darkened open ground to 0.83 and 0.98 (reference 1.0); the comment at the switch says so.
- **Background handling**: pixels at the far plane (or with a cleared normal) write AO 1 and full edges, and linear
  depth is clamped finite to R16F's range. Found on the web: Chrome rendered the sky as AO 0 (a non-finite background
  depth turned the denoiser's weights into NaN, which WGSL's clamp() does not scrub; the Vulkan driver's happened to).
- **Docs**: ProjectSettings.xml (`ssao/method`), RenderingServer.xml (method + enum); `--doctool` agrees (it also
  rewrote CameraAttributes.xml, unrelated; restored).
- **Web**: 5 variants added to `wgsl_precompile.py` (297 compiled). The linear depth texture is deliberately not named
  "depth" (the driver reclassifies such single-channel textures to `texture_depth_2d`); comment in the shader.
- **Forward+ matrix**: new arm `gtao` (the ssao arm's settings with method GTAO; `_reset()` sets ASSAO for every other
  arm), `features.mjs` minDelta 1.39 (a third of the measured 4.17).
- **Interface references** regenerated: `interface_reference.json` +5 modules (`gtao.glsl_{prefilter,main,denoise,
  denoise_final,upsample}_comp.wgsl`, source line appended), `scan_reference.txt` +10 facts (the file renumbers its
  fact table, hence the large diff; nothing existing changed: 584/584 identical before regenerating).

## Verification (numbers.txt has everything; tools/ has the scripts)

Test scene: ground plane, two boxes, a wall, a sphere; 960x540; AO buffer read through the SSAO debug draw.
Reference: `tools/rt_reference.py` ray traces the same analytic geometry in numpy (256 stratified cosine-weighted rays
per pixel, occluded if hit within the AO radius), i.e. the visibility GTAO estimates. Picture: gtao_vs_reference.png.

| over 369k geometry pixels | MAE | corr | occluded-region bias | open ground |
|---|---|---|---|---|
| ASSAO Medium (ASSAO, all levels: MAE 0.037-0.048) | 0.037 | 0.83 | +0.150 (too light) | 0.999 |
| GTAO Very Low 2x2 | 0.041 | 0.85 | +0.117 | 0.983 |
| GTAO Low 3x2 | 0.033 | 0.88 | +0.118 | 0.999 |
| GTAO Medium 3x3 (full / half) | 0.028 / 0.027 | 0.90 | +0.051 / +0.048 | 0.999 |
| GTAO Ultra (14x4) | 0.028 | 0.91 | +0.031 | 0.999 |
| GTAO Medium, WebGPU (Chrome) | 0.028 | 0.90 | +0.052 | 0.999 |

- **Default unchanged, bit-identical**: method left at its default, new editor vs the pre-change editor, AO buffer and
  final image: max pixel difference 0 (also checked: method=off, and ASSAO set explicitly).
- **WebGPU vs native** (system Chrome with the Vulkan flags; adapter nvidia/lovelace, not fallback, 48 textures,
  asserted): GTAO statistics match native to 0.001; per-pixel mean |d| 0.70/255 (p99 11), noise-like in occluded areas
  (the noise pattern's float arithmetic differs between compilers); sky 255 on both; 0 console errors or warnings.
- **Temporal**: without TAA the noise is fixed (two frames of a still camera differ by 0). With TAA the noise index
  advances per frame (XeGTAO's design): final-image temporal std 0.16/255 mean (p99 2.3) vs ASSAO 0.02 (p99 0.46), a
  slight residual shimmer.
- **Cost** (1920x1080, RTX 4080 SUPER, native Vulkan, `--gpu-profile`, AO passes only, median of per-second averages;
  the scene is tiny, so expect +-0.1 ms noise):

  | quality | GTAO full | GTAO half | ASSAO full | ASSAO half (default) |
  |---|---|---|---|---|
  | Very Low | 0.64* | 0.27* | 0.37 | 0.11 |
  | Low | 0.72* | 0.37* | 0.52 | 0.27 |
  | Medium | 0.53 | 0.31 | 0.63 | 0.29 |
  | High | 2.01 | 0.55 | 1.39 | 0.72 |
  | Ultra | 3.31 | 1.26 | 0.60 | 0.91 |

  \* measured with XeGTAO's 1x2 / 2x2 before the remap; the remapped 2x2 / 3x2 cost a little more. GTAO Medium at half
  size costs what ASSAO's default costs. Web GPU timing was not measured.
- **Forward+ matrix** (`WEBGPU_REAL_GPU=1 run_forward_plus.mjs --parity --feature ssao,gtao,ssil`, debug template from
  this tree): ssao PASS (delta 1.673, native diff 0.008), ssil PASS (1.744, 0.005), gtao PASS (4.170, 0.011).
- Editor built with `dev_mode=yes` and the web debug template: both clean. `pre-commit` clean.
- **smoke.sh** (final tree):
  ```
  [WGSL Precompile] Results: 297 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 297/297, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 594/594 identical, 0 differ, 0 missing, 0 not in reference
  validation: 297 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261028-143724-1764513)
  ```

## What the lead must still run

- After merging: regenerate the interface and scan references (other workers add modules too), smoke, and the full
  Forward+ matrix with `--parity` (one more arm: `gtao`).
- Not run here: a baked (xvfb) export (the web runs used unbaked exports, i.e. runtime conversion; the precompile
  table covers the five variants), Firefox, web GPU timing.

## Risks / limits

- Forward+ only. Mobile has no SSAO; Compatibility keeps its own (method ignored, documented).
- TAA residual shimmer (above). If it matters, the noise can stay fixed under TAA too (`noise_index`, one line).
- Ultra's adaptive target sets the slice count; there is no ASSAO-style adaptive sampling.
- No bent normals or multi-bounce term: the output is the same single-channel AO texture.
- Untested: multiview/XR (per-view layers allocated like ASSAO), orthographic cameras (code path present), MSAA (uses
  the resolved depth/normal ASSAO uses).
