# Task 36: exposure automation (metering modes, auto white balance): SUMMARY

Branch `work/36` on a51fa557fb (webgpu-4.8 with task 33 merged). Worker-36, 2026-10-28.

## What changed

**CameraAttributes** (both kinds, base class):
- Auto Exposure group: `auto_exposure_metering_mode` (`METERING_MODE_AVERAGE` default, `_CENTER_WEIGHTED`, `_SPOT`,
  `_HIGHLIGHT_WEIGHTED`), `auto_exposure_metering_spot_size` (% of the frame, 2.5), `auto_exposure_metering_spot_point`
  (UV, 0.5,0.5), `auto_exposure_metering_highlight_percentile` (99). Each shown only for the mode that uses it.
- White Balance group: `white_balance_auto` (off), `white_balance_auto_speed` (1/s, 1.0), `_min_temperature` (2500 K),
  `_max_temperature` (10000 K). The manual temperature/tint stay, as a bias on top and as the starting point.
- RenderingServer: `camera_attributes_set_auto_exposure_metering()`, `camera_attributes_set_auto_white_balance()`,
  `RS.CameraAttributesMeteringMode`. Class reference XML for all of it (purely additive; checked with `--doctool`).

**Model** (comment blocks "Exposure automation" and "Auto white balance" in `camera_attributes_storage.cpp`):
- Every mode meters the value Average meters, max(R, G, B) in scene-linear light, and goes through the same exposure
  formula (scale / value), adaptation and sensitivity clamp, so a mode only changes where and how the meter looks.
  Average = the original reduction, untouched. Center-weighted = Nikon's 75% weight in a 12 mm circle on a 36x24 mm
  frame (F100/F5/F6 specifications), i.e. a circle half the frame height across. Spot = a circle of N% of the frame
  (Canon 1.3-4%, Nikon 1.5%). Highlight-weighted = the P-th percentile placed at 1.0 by the default scale 0.4 (scale
  stays the exposure compensation). The weighted modes take the arithmetic weighted mean, not a log-average (a meter
  integrates light linearly, ISO 2720; question 1).
- AWB: gray world (Buchsbaum 1980) over near-gray pixels: Gaussian weight (sigma 0.25) on ln(R/G), ln(B/G) after the
  current correction (Huo et al. 2006's gray-pixel selection, iterated by the frame loop), rejecting pixels the
  tonemapper would clip (exposure computed as the tonemapper does, auto exposure included). The estimate becomes a
  CCT/Duv on the Krystek locus (golden-section search for the nearest point; the locus the manual white balance and
  Light3D use), clamped to the range and +-0.05 Duv, adapted in CIE 1960 uv by 1 - exp(-speed dt), and turned into von
  Kries gains to display white in the manual white balance's Bradford cone space.

**GPU** (`CameraMetering`: `renderer_rd/effects/camera_metering.{h,cpp}`, `shaders/effects/camera_metering.glsl`, 7
compute variants, created on first use):
- Weighted modes: METER_READ writes per-8x8-block sums (w·v, w) into an RGBA32F chain whose levels round **up**,
  REDUCE sums down to 1x1, METER_RESOLVE divides, adapts and writes `LuminanceBuffers::reduce[last]`, swapped in as
  `current` exactly as `luminance_reduction()` ends, so the tonemapper, glow and every other reader of the exposure
  texture are untouched.
- Highlight: HISTOGRAM counts log2(v) into 256 bins (1/8 EV, 2^-16 to 2^15.75) with workgroup atomics into a storage
  buffer (`buffer_clear` per frame); HISTOGRAM_RESOLVE walks it once and interpolates in log2 within the bin.
- AWB: AWB_READ (sums of w·RGB, w) through the same chain, AWB_RESOLVE writes a 2x1 RGBA32F state (texel 0 gains,
  texel 1 uv + valid); two state textures ping-pong per viewport (WebGPU has no read-write RGBA32F storage textures).
- Forward+ tonemap: `camera_exposure_inc.glsl`, the state texture at **set 1 binding 2**, gains read when
  `CameraEffectsUBO.header.z & EXPOSURE_AUTO_WHITE_BALANCE` (bit 1). `_fill_effects_exposure()` sets it only when the
  renderer ran the AWB passes (`CameraEffectsContext::auto_white_balance`, new member). The `exposure[2]` region stays
  zero (nothing needed it). Manual and auto gains multiply (both diagonal in the same cone space).
- `camera_attributes_uses_effects_buffer()` is deliberately **not** extended: Mobile and Compatibility cannot run AWB,
  so it must not cost Mobile its subpass or force Compatibility's post pass.
- Mobile and Compatibility: unchanged. Metering falls back to Average (auto exposure is Forward+ only anyway), AWB to
  the manual white balance; `WARN_PRINT_ONCE_ED` like auto exposure's. Documented in the class reference.
- `wgsl_precompile.py`: the 7 variants. Interface references regenerated: `(1, 4, sampler), (1, 5, texture_2d)` (the
  new binding after combined-sampler splitting) in the four tonemap fragment variants, plus the 7 new modules; the
  scan reference only renumbered around them.

## Verification (all numbers in `numbers.txt`, scripts in `tools/`)

Native: xvfb, RTX 4080 SUPER, Vulkan, harness `tools/meter_test.gd`. Metering and AWB are measured on HDR 2D captures
(RGBA16F, Linear tonemapper): the exposure gain is read as output / AE-off frame, and the references are computed in
Python from the AE-off frame following the definitions (pixel-center circles, Nikon weights, spot area,
`np.percentile`; AWB with textbook matrices and colour-science's Ohno 2013 CCT), not from the shader.

- **Bit-identical defaults**: pre-change editor (a51fa557fb, built in this worktree) vs work/36, 6 scenes (lights +
  shadows + sky + glow with no/Practical/Physical attributes, Average auto exposure, manual white balance 4500 K/10, the
  AWB room with auto exposure) x Forward+/Mobile/Compatibility: **max diff 0, 0 differing pixels, 18/18**.
- **Metering** (dark interior 0.01-0.06, window 2-12 at the upper right, subject 0.1-0.3 in the middle, four lamps
  40-200), metered value vs reference at 1920x1080 / 1280x720:
  - Center-weighted 0.46902 vs 0.46885 / 0.46714 vs 0.46698; spot at the center 0.17553 vs 0.17547 (both); spot (1%)
    at (0.75, 0.3), inside the window, 5.9137 vs 5.9117 (both): **+0.0005 EV everywhere**, the RGBA16F quantum of the
    gain readout. The off-center spot also proves the UV orientation.
  - Highlight 99th 4.3420 vs 4.3375 (+0.0015 EV) / +0.0001 EV; 90th +0.0014 / -0.0050 EV; 99.9th at scale 0.8 (on the
    lamps' steep gradient) +0.020 / -0.029 EV: within a quarter of the 1/8 EV bin.
  - Average matches a Python model of the original reduction within +0.0005 EV (unchanged path; see risks).
- **AWB** (room lit by a 3200 K blackbody, gray card, white walls, red/green/blue/yellow objects, manual 6500 K,
  speed 1): gray card r/g 1.939 b/g 0.376 (|duv| 0.0565 from white) -> 1.705/0.436 at 0.23 s -> 1.162/0.754 at
  1.53 s -> 1.028/0.946 at 3.24 s -> **1.0000/1.0002, |duv| 0.00001** converged (speed 3, 5.4 s and 7.4 s). The
  independent Python estimator finds the light at 3200 K, Duv 0.0000, and predicts that card to |duv| 0.00000. |duv|
  decays at 0.86-0.94/s for speed 1 (expected 1/s; the gray selection sharpens as it converges).
  - Clamp (min 4000 K): card 1.3251/0.6801 vs predicted (corrected for a 4000 K blackbody) 1.3272/0.6827.
  - Bias (manual 5000 K, tint 20 on top): 0.9147/1.5151 vs the manual model's prediction 0.9147/1.5146.
  - White (1,1,1) light: card stays 0.9994/1.0003 (|duv| 0.00003), whole frame within 0.001 (RGBA16F) of AWB off;
    6500 K blackbody light: card 0.9998/0.9998.
- **WebGPU** (system Chrome, headed, Vulkan; adapter nvidia/lovelace, maxSampledTexturesPerShaderStage 48, engine
  "WebGPU 1.0 - Forward+"), 13 cases at 1280x720 vs the native Vulkan Forward+ renders: all 7 metering cases
  **pixel-identical** (max diff 0); AWB on/clamp/bias max diff 1/255, 0 pixels above 2, card ratios equal to 4 digits
  (on: 1.0002/1.0001 web vs 1.0002/1.0002); default scenes max 1.
- **Cost** (viewport GPU time, median, 1920x1080, AWB room on the 4080 SUPER): none 0.174 ms, Average AE 0.215,
  center/spot 0.218, highlight 0.197 (one pass), AWB 0.225, center + AWB 0.267: about +0.05 ms per feature.
- **Fallbacks**: Mobile and Compatibility with AWB + highlight metering render without errors and warn once.
- **Smoke** (final code; only docs and results changed after it):
  ```
  [WGSL Precompile] Results: 292 compiled, 0 glsl failures, 0 conversion failures
  identical interface: 292/292, mismatched: 0, missing in candidate: 0, not in reference: 0
  against the stored reference: 584/584 identical, 0 differ, 0 missing, 0 not in reference
  validation: 292 modules, 0 invalid, 0 of them expected
  SMOKE OK (smoke-20261028-101300-542769)
  ```

Not verified: MSAA, XR/multiview (both use the view-0 internal texture as auto exposure does), scaled viewports,
Android (no float32-filterable: see risks), a `dev_mode=yes` (werror) build.

## What the lead must run

- After merging: regenerate the interface references again (35 may also add to the tonemap), then smoke. **Task 35 must
  not take tonemap set 1 binding 2.**
- `./webgpu_tests/local_ci.sh --dev-mode` (I built without werror).
- Optionally rerun on the merged tree: put `tools/meter_test.gd` in a scratch project with `rendering/anti_aliasing/
  quality/msaa_3d=0` and fix the paths at the top of `tools/r.sh`, then `tools/meter_runs.sh <editor> <out>` +
  `tools/analyze_meter.py <out>`, `tools/ident_runs.sh` against a pre-merge editor, the AWB cases in `tools/gpu_job.sh`
  + `tools/analyze_awb*.py` (needs colour-science), the web cases with `tools/shoot.mjs` + `tools/compare_web.py`.

## Risks

- **Pre-existing, upstream**: Average metering drops the right and bottom edge blocks (`floor(size / 8)` levels): the
  right 20% of a 1080p frame, the right 20% and bottom 27% of a 720p one. Left as is to stay bit-identical; question 2
  in `for-tasks.md`.
- AWB is a gray-world method: a frame dominated by one color is partly corrected toward gray (documented; the
  temperature range is the control).
- Devices without `float32-filterable` (some Android Chrome): the driver substitutes its fallback for the RGBA32F state
  texture exactly as it already does for the R32F auto exposure texture there, so AWB reads no gains there (and auto
  exposure is already broken the same way).
- The AWB state is per viewport and resets when the render buffers are reconfigured (resize), as auto exposure's does.
- Questions for the user (defaults built): arithmetic vs log-average metering, fixing Average's dropped edges, display
  white vs the manual model's 6500 K white as AWB's neutral (`for-tasks.md`).
