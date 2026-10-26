# For TASKS.md (task 28)

## §4 entry (after §4.43)

### 4.44 NaN guards for glow, SSR and auto exposure (October 2026) *(work task 28)*

User decision 2026-10-26 (hand-offs `nan-guards`: "Guard all three now"), the follow-up §4.41 left open. The bokeh
DOF's guard moved to a shared include, `servers/rendering/renderer_rd/shaders/effects/finite_inc.glsl`
(`finite_or_zero()` / `finite_or_zero3()` / `finite_or_zero4()`, `is_non_finite*()`; distinct names because WGSL has no
overloads), and every effect that reads the scene color now passes it through once, where it is read:

- **Glow:** Forward+ `copy.glsl` `MODE_GLOW` first pass, the four `local_cache` loads per thread (inside the existing
  `FLAG_GLOW_FIRST_PASS` branch); Mobile `blur_raster.glsl` `MODE_GLOW_GATHER`, its four samples. Later levels read
  that output.
- **Auto exposure:** `luminance_reduce.glsl` `READ_TEXTURE` (once per pixel) and `luminance_reduce_raster.glsl`
  `FIRST_PASS` (once per texel read). The raster path only runs where storage is unavailable; Mobile does not
  apply auto exposure at all (its own warning), so it is guarded but not observable there.
- **SSR:** its input is `RB_LAST_FRAME`, made by `copy_to_rect(..., sanitize)` and `make_mipmap()`, which upstream
  already sanitized: inf (|x| > 3e10) and NaN (`notEqual(color, color)`) to 100. **That NaN test never fired on the
  4080** although glslang emits the correct `OpFUnordNotEqual` (the driver assumes no NaN). It is now
  `finite_or_zero4()`: NaN counts as black, infinity is still 100 from upstream's line. The SSR shaders themselves are
  unchanged. Side effect: every other `make_mipmap()` user (screen-texture mips for refraction/`SCREEN_TEXTURE`,
  runtime texture mip generation) now turns a NaN into 0 instead of (in intent) 100, and actually catches it.
- **Bokeh DOF** uses the shared helper; same code as before (`dof_finite()` is gone).

Numbers (4080 SUPER, native Vulkan and WebGPU Chrome real adapter; before = base `2eca8b0cf5` built in the same tree):

| Check | Before | After |
|---|---|---|
| `dof_nan/run.sh` Forward+ glow additive / softlight (pixels changed) | 59,791 / 6,434 (a blob at the luminance cap: NVIDIA's `min()` drops the NaN) | 4 / 4 |
| `dof_nan/run.sh` Mobile glow additive / softlight | 1,591 / 200 | 4 / 4 |
| `dof_nan/run.sh` SSR roughness 0 / 0.4 (black pixels) | 212 / 23,812 (the second after the first's NaN persisted) | 4 / 4 |
| `dof_nan/run.sh` auto exposure, Forward+ (mean brightness) | 0.4001 → 0.0000, whole frame | 0.4001 → 0.4001 |
| Tier total | 7 failures | 0 (all 24 DOF cases still pass) |
| Matrix `glow_nan` / `ssr_nan` / `auto_exposure_nan`, native delta | 1.261 / 28.684 / 97.513 | 0.005 / 0.004 / 0.003 |
| Same arms, WebGPU (unbaked) | FAIL 1.261 / 28.685 / 97.514 | PASS 0.005 / 0.004 / 0.003, parity ≤ 0.003 (baked too) |

Finite frames bit-identical natively: `glow`, `glow_map`, `ssr`, `ssil`, `refraction`, `auto_exposure`, `dof_far`,
`dof_near`, `dof_nan` and the off frames of the new arms, 0 differing pixels before vs after. WebGPU `--parity` on
the 10 guarded/new arms passes unbaked and baked (translated 133, under the 150 gate). Interface references unchanged
(smoke: 276/276 identical, 552/552 scan facts, 0 invalid).

Cost: an AND, a compare and a select per component (12 ALU ops per vec4): Forward+ glow first pass 48 per thread at
half resolution (~12 per scene pixel), Mobile gather 48 per quarter-resolution fragment, auto exposure 9 per pixel,
the last-frame copy +4 per pixel over upstream's test. Once per frame each; well under a microsecond at 1080p on the
4080, not separable from noise, so not measured with `gpu_time.mjs`.

Tests: `webgpu_tests/dof_nan/run.sh` now covers glow (additive, softlight), SSR (roughness 0, 0.4; Forward+ only, a
reported SKIP on Mobile) and auto exposure as well (`ONLY=dof,glow,ssr,exposure` narrows), gated also on changed
pixels (glow ≤ 64, SSR ≤ 256) and auto exposure on mean brightness (≤ 2 %). Forward+ matrix arms `glow_nan`,
`ssr_nan`, `auto_exposure_nan` (`maxDelta` 0.05; 87 arms).

## §3 trap 70 — append

Follow-up (§4.44): glow, SSR and auto exposure spread it too, and not always as black. On NVIDIA, `min(NaN, cap)`
returns the cap, so glow turned one NaN pixel into a **bright** blob, and `notEqual(x, x)` is false for a NaN, so
upstream's own SSR sanitize let it through; auto exposure blanked the whole frame. Measure "pixels changed", not only
"pixels black", and test NaN on the exponent bits (`finite_inc.glsl`), which is the only test that held on every path.
A NaN in SSR's input also persists for frames after its source is gone (seen as black bars in a NaN-free reference).
