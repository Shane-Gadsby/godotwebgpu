# 28-nan-guards-glow-ssr-exposure: guard glow, SSR and auto exposure against a non-finite pixel, as the DOF is

State: ready-for-integration
Owner: worker-28
Claimed: 2026-10-26 19:30
Needs: build, gpu (parity)
Branch: work/28
Results: webgpu_notes/work/results/28-nan-guards-glow-ssr-exposure/

## Context

User decision, 2026-10-26 (hand-offs page `nan-guards`: "Guard all three now"). Task 26 (TASKS.md §4.41, §3 trap 70)
made the bokeh DOF treat a non-finite scene color as black with `dof_finite()` (`bokeh_dof_inc.glsl`: a bit test on the
exponent and a select, ~4 ALU ops, finite values pass bit-identical), so one NaN pixel stays one pixel. Glow, SSR and
auto exposure still spread one: glow blurs it over its radius, SSR reflects it, and auto exposure averages it into the
luminance, which can blank or darken the whole frame.

## Goal

1. The same guard in **glow** (the first glow downsample that reads the scene color; Forward+ compute and Mobile's
   raster path), **SSR** (where it reads the scene color it reflects) and **auto exposure** (the first luminance
   reduction step; compute and raster variants). Guard once at the input where possible, not per tap. Move the helper
   to a shared include (one definition), keep `bokeh_dof` using it.
2. Finite frames bit-identical: the `glow*`, `ssr`, `auto_exposure`, `dof_*` arms of `run_forward_plus.mjs --parity`
   unchanged, unbaked and baked.
3. A regression check per effect: extend `webgpu_tests/dof_nan` (or generalize it, e.g. `nan_containment`) so one NaN
   pixel goes through glow, SSR and auto exposure natively on Forward+ and Mobile, and a Forward+ matrix arm (or arms)
   for the web, like `dof_nan` (`maxDelta`). Before/after numbers in SUMMARY.md.
4. Say what each guard costs (ALU per pixel; GPU time with `perf_guard/gpu_time.mjs --passes` if measurable).

If the interface references change, regenerate them deliberately and say why. Shared docs text goes in
`for-tasks.md` / `for-docs.md` (TASKS.md §4 entry, the §3 trap 70 follow-up, the CORRECTNESS doc's §2.9b, the wiki's
Notable Fixes line).

## Verification

`webgpu_notes/work/smoke.sh`; the NaN tier native; `--parity` for the guarded arms and the new ones, unbaked and baked.
