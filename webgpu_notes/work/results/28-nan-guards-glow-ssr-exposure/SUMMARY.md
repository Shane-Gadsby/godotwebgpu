# Task 28: NaN guards for glow, SSR and auto exposure — SUMMARY

State: ready-for-integration (branch `work/28`).

## What changed

- New shared include `servers/rendering/renderer_rd/shaders/effects/finite_inc.glsl`: `finite_or_zero()`,
  `finite_or_zero3()`, `finite_or_zero4()` (non-finite → 0, tested on the exponent bits with a select; finite values
  pass bit-identical). Distinct names, since WGSL has no overloads. The bokeh DOF uses it (`dof_finite()` removed from
  `bokeh_dof_inc.glsl`, which now includes it).
- **Glow**: Forward+ `copy.glsl` first glow pass guards its four `local_cache` loads; Mobile `blur_raster.glsl`
  `MODE_GLOW_GATHER` guards its four samples. Each scene texel is tested once.
- **Auto exposure**: `luminance_reduce.glsl` (`READ_TEXTURE`, once per pixel) and `luminance_reduce_raster.glsl`
  (`FIRST_PASS`, per texel read).
- **SSR**: its input, `RB_LAST_FRAME`, is made by `copy.glsl`'s sanitize copy and `MODE_MIPMAP`, which upstream
  already meant to sanitize (`notEqual(color, color)` → 100). That test never fired on the 4080 (glslang emits the
  correct unordered compare; the driver assumes no NaN). Replaced with `finite_or_zero4()` (NaN → black; inf still 100
  from upstream's `abs > 3e10` line). SSR shaders untouched. This also affects other `make_mipmap()` users (screen
  texture mips, runtime mip generation): a NaN there becomes 0, and is now actually caught.
- Tests: `webgpu_tests/dof_nan/` extended (glow additive/softlight, SSR roughness 0/0.4 on Forward+, auto exposure;
  `ONLY=` narrows; gated on black pixels, changed pixels for glow/SSR, mean brightness for auto exposure). Forward+
  matrix arms `glow_nan`, `ssr_nan`, `auto_exposure_nan` (`maxDelta` 0.05), 87 arms; `nan_quad` gets a reset transform.

## Before / after (4080 SUPER; before = `2eca8b0cf5` built in this tree)

| | Before | After |
|---|---|---|
| tier, Forward+ glow additive / softlight, px changed | 59,791 / 6,434 (bright blob: `min(NaN, cap)` = cap on NVIDIA) | 4 / 4 |
| tier, Mobile glow additive / softlight | 1,591 / 200 | 4 / 4 |
| tier, SSR roughness 0 / 0.4, black px | 212 / 23,812 | 4 / 4 |
| tier, Forward+ auto exposure, mean | 0.4001 → 0.0000 (whole frame) | 0.4001 → 0.4001 |
| tier result (`run.sh`) | 7 failures | 0 failures, Forward+ and Mobile (24 DOF cases still pass) |
| matrix glow_nan / ssr_nan / auto_exposure_nan, native delta | 1.261 / 28.684 / 97.513 | 0.005 / 0.004 / 0.003 |
| same, WebGPU unbaked | FAIL 1.261 / 28.685 / 97.514 | PASS 0.005 / 0.004 / 0.003 |

Mobile does not apply auto exposure (its own warning), so the raster luminance guard is not observable there.

**Finite frames**: native renders of `glow`, `glow_map`, `ssr`, `ssil`, `refraction`, `auto_exposure`, `dof_far`,
`dof_near`, `dof_nan` (off and on) and the new arms' off frames are **bit-identical** before vs after (0 pixels differ).
WebGPU `--parity` on glow, glow_map, glow_nan, ssr, ssr_nan, auto_exposure, auto_exposure_nan, dof_far, dof_near,
dof_nan: 10/10 PASS unbaked and baked (baked: translated 133, under the 150 ceiling), parity diffs 0.002–0.003, same
deltas as the arms had (glow 43.175, ssr 1.182, auto_exposure 35.715, dof_far 8.124, dof_near 1.844).

## Cost

AND + compare + select per component, 12 ops per vec4: Forward+ glow first pass 48/thread at half res (~12 per scene
pixel), Mobile gather 48 per quarter-res fragment, auto exposure 9 per pixel, last-frame copy ~+4 per pixel over
upstream. Once per frame each, sub-microsecond at 1080p on the 4080; too small to separate from noise, so I did not
run `gpu_time.mjs --passes`.

## Smoke (final tree)

```
identical interface: 276/276, mismatched: 0, missing in candidate: 0, not in reference: 0
against the stored reference: 552/552 identical, 0 differ, 0 missing, 0 not in reference
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261026-195803-721562)
```

Interface references unchanged (no regeneration).

## What the lead must still run

- On the merged tree: `webgpu_notes/work/smoke.sh`; `webgpu_tests/dof_nan/run.sh` (needs a display);
  `run_forward_plus.mjs --parity` (and `--baked`) for at least the 10 arms above, ideally the full 87.
- Fold `for-tasks.md` (§4.44, trap 70 follow-up) and `for-docs.md` (CORRECTNESS §2.9b, tests README 87 arms,
  CLAUDE.md arm count, wiki Notable Fixes row).

## Risks

- Deliberate divergence from upstream in `copy.glsl`, `blur_raster.glsl`, `luminance_reduce*.glsl`, `bokeh_dof*.glsl`;
  an upstream sync may conflict there.
- `make_mipmap()`/sanitize copy: NaN → 0 instead of upstream's (intended) 100, for all its users, not only SSR.
- A NaN from a source other than the scene color (normal/roughness buffer, depth) is not guarded.
- My first web template build (19:42) overlapped the user's `build-linux.sh` in the lead's tree (different trees, no
  shared sconsign); after the lead's note I ran no SCons until it finished.
