# 26: strand shader NaN and the bokeh DOF black hexagon — summary (worker-26, 2026-10-26)

State: ready-for-integration (branch `work/26`)

## The NaN source, proven

The eyelash groom tapers to a width of **exactly 0** at every strand tip, so `v_coverage = 0` at those vertices; at
some pixels beside a tip the interpolated `v_coverage` comes out at **-1.07e-9**, and `pow(-1.07e-9, 0.5)` is NaN on
NVIDIA (and on lavapipe). The leading theory was right in substance; the evidence is new.

1. **Read-back, no probe in the strand shader** (`tools/nan_probe.gd`, a `CompositorEffect` after the transparent
   pass, before DOF; native Vulkan, RTX 4080 SUPER, the user's own import, 4.80 m / default height): exactly **one**
   non-finite pixel, (528, 288) in the 1088x612 internal buffer (3D scale 0.85 of the 1280x720 SubViewport = the
   user's (621, 338)), value `7fffe000` in **all four channels**. Blend math (`blend_mix`: dst.a = a + dst.a(1-a))
   makes the alpha channel NaN only if a fragment's `ALPHA` was NaN, and the scene shader's alpha is the material's
   `ALPHA` times a uniform. So the NaN is in the `ALPHA` expression, not the anisotropic lighting (which can only reach
   rgb) — the alternative theory is ruled out.
2. **Which value**: the lash material swapped for one with the identical vertex stage and an **additive** fragment
   (`tools/probe_count.txt`, `probe_value.txt`) — counts, at each pixel, fragments whose `v_coverage` has the sign bit
   set / is NaN, and encodes log2 of a negative value. At exactly (528, 288): 92 lash fragments, **one** with
   `v_coverage < 0` (not -0, not NaN), log2(-v) = -29.8 → **v ≈ -1.07e-9**, at **v_t ≈ 1.0** (the tip). No other pixel
   in the 16x16 region has one. (`sweeps/probe_value_at_nan_pixel.txt`)
3. **Why tips**: mesh stats on the import — eyelashes: 1574 of 18888 vertices have `UV2.x` (groom width) == 0, all
   at `UV.y > 0.999` (787 strands x 2); eyebrows and scalp hair: none (min nonzero 0.0099 / 0.044). So only the lashes
   can produce coverage 0, and only beside their tips.
4. **Why the user's markers never fired**: the bad fragment is one of 92 overlapping lash layers at that pixel. A
   blended marker is buried by the ~91 layers drawn after it; an additive probe cannot be. And every probe replaced
   the `pow()` that made the NaN, which is why probes "hid" the fault.

Other MetaHuman shaders checked for the pattern: no other `pow()` of an interpolated value; `HAIR_SHELL`'s
`normalize(v_binormal)` is NaN for a zero binormal (TANGENT along NORMAL) in an opaque pass, now guarded. `SKIN` and
`CLOTH` `normalize(vec3(base_n.xy + ..., base_n.z))` could only be zero for an exactly cancelling normal-map texel
(not reachable with 8-bit maps); left alone (task 25 owns SKIN), noted for the lead.

## What changed

- `modules/metahuman/editor/metahuman_shaders.h`: `HAIR_STRANDS` `ALPHA = pow(clamp(v_coverage, 0.0, 1.0), ...)`;
  new `HAIR_STRANDS_ALPHA_LINE`; `HAIR_SHELL` binormal normalized with a floor. SKIN untouched.
- `editor_scene_importer_metahuman.cpp`: the opaque and hashed strand variants replace `HAIR_STRANDS_ALPHA_LINE`
  instead of a copy of the line (the old copies would have silently stopped matching), the hashed one clamps too,
  and an `ERR_PRINT` fires if the constant ever stops matching the shader.
- DOF hardening, `bokeh_dof_inc.glsl`: `dof_finite()` — non-finite rgb counts as black, tested on the exponent bits
  (compilers may fold `isnan()`, WGSL allows it) and applied with a select, so every finite value passes bit for bit.
  - compute path (Forward+, all of box/hexagon/circle, near/far, physical or not): once per pixel in
    `MODE_GEN_BLUR_SIZE`, which already reads and rewrites every pixel of the color image the later passes read.
  - raster path (`bokeh_dof_raster.glsl`, Forward Mobile): no pass rewrites the color, so on each color fetch of the
    box/hexagon/circular passes.
  - **Cost**: compute, ~4 ALU ops per pixel in a bandwidth-bound full-screen pass; raster, ~4 ops per filter tap
    (a tap already costs two texture fetches and a smoothstep). Not timed — below what `gpu_time.mjs` resolves; the
    WGSL keeps it as one `select` on `bitcast<vec3u>` (checked in the smoke run's output).
  - Finite images unchanged: native `dof_far` / `dof_near` / `dof_nan` off frames old vs new engine, mean diff **0.0**.

## Regression checks added

- `webgpu_tests/dof_nan/run.sh` (native, needs a display): a 2x2 px NaN quad through 3 shapes x near/far x
  transition/physical, on Forward+ (compute) and Mobile (raster, HDR viewport so its buffer can hold a NaN).
  Before: Forward+ 12/12 FAIL (1060–4624 black px), Mobile 6/12 FAIL. After: 24/24 PASS (0–4 black px, the quad).
- Forward+ matrix arm `dof_nan` (`maxDelta: 0.05`): web before (old template) **FAIL 1.068**, native before 1.07;
  after: native 0.0013, **web PASS 0.001, parity 0.003** (with `dof_far` 8.12 / `dof_near` 1.84 PASS, parity ≤0.003).
- `webgpu_tests/metahuman/nan_check.gd` (in `run.sh`, lavapipe under Xvfb): forces every brow/lash vertex to
  `v_coverage = -1e-9`; old unclamped shader 7032 non-finite pixels (the control, else SKIP), current 0. Plus a check
  that no strand shader has `pow(v_coverage,`. `run.sh ~/Downloads/Ava.zip`: verify 13/13, nan 5/5 PASS.

## The 42-position sweep (distance 4.70–4.90 @ 1.64 m, height 1.55–1.75 @ 4.80 m; 4.80/1.64 is in both)

Failing = a non-finite pixel before DOF, or >50 black pixels on the face (`sweeps/*.txt`).

| | native Vulkan (RTX 4080 S) | WebGPU (Chrome, real adapter, Forward+) |
|---|---|---|
| before (old engine, old import) | **3/42** (4.80@1.63, 4.80@1.64 x2), 4688 black px each | 0/42, no NaN anywhere |
| DOF fix alone (new engine, old import) | NaN still at the same 3, **black face px 0** — contained | not run (no NaN on web to contain) |
| both (new engine, re-imported `head.dna`) | **0/42**, 0 non-finite | **0/42**, 0 non-finite |

The web never produced the NaN at these positions with the old shader (rasterizer/compiler specific), but it spreads
one exactly as natively once one exists (the `dof_nan` arm, 1.068 before). Frames: `looks/`.

## Verification already run

- `webgpu_notes/work/smoke.sh`: `276 compiled, 0 conversion failures`; interface 276/276 identical; text-scan
  552/552 identical; Chrome 276 modules, 0 invalid; **SMOKE OK**. No interface references regenerated.
- Builds in the worktree: editor (`webgpu=yes`), web `template_release` nothreads non-dlink, same commit.

## What the lead must still run

- Full `run_forward_plus.mjs --parity` (I ran `dof_far,dof_near,dof_nan` only) — the new arm makes it 83.
- `webgpu_tests/dof_nan/run.sh` on the merged tree; `webgpu_tests/metahuman/run.sh ~/Downloads/Ava.zip`.
- The user's own project must re-import `scenes/Ava/head.dna` (the shader lives in the import) — or rely on the DOF
  fix, which contains the NaN without it.

## Risks

- `dof_finite()` touches upstream Godot shaders (`bokeh_dof*.glsl`); merge conflicts on an upstream sync are possible.
- A NaN pixel now renders black (one pixel) instead of a hexagon; the NaN itself still exists if a shader makes one.
- Glow, SSR, auto-exposure etc. would still spread a NaN; only DOF is hardened (scope of this task).
