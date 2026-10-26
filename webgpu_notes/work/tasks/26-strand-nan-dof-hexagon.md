# 26-strand-nan-dof-hexagon: the MetaHuman strand shader writes a NaN, and the bokeh DOF spreads it into a black hexagon

State: open
Owner: worker-26
Claimed:
Needs: build, gpu (native and web captures)
Branch: work/26
Results: webgpu_notes/work/results/26-strand-nan-dof-hexagon/

## Context

User report, 2026-10-26, with the user's own findings (kept verbatim in substance). In the camera-sim project
(`/mnt/109313D2109313D2/godot-game-dev/camera-sim`, a git repo of the user's: **never modify it**, work on a scratch
copy, LIVE_REPRO_METHODOLOGY.md), a solid black hexagon covers Ava's face at one exact camera position: 4.80 m from the
head, height 1.63–1.64 m (eye level, straight on). 4.79 m, 4.81 m and 1.62 m are clean. It reproduces on native Vulkan
(RTX 4080): not WebGPU-specific.

What the user found:
- Source: the `eyelashes_strands` mesh, `MetaHumanShaders::HAIR_STRANDS` (`modules/metahuman/editor/metahuman_shaders.h`,
  the `ALPHA = pow(v_coverage, coverage_exponent) * ...` line near 301). Hiding only that mesh removes the fault; hiding
  the eyes, eye shell, lacrimal edge or eyebrows does not.
- With DOF off, a 2×3 block of pure-black pixels remains at about (621–622, 338–340) in the 1280×720 SubViewport.
- Why a hexagon: in `bokeh_dof.glsl` `weighted_filter_dir()`, `accum += mix(color, sample_color, m)` keeps a NaN sample
  even when `m == 0` (NaN × 0 is NaN), so every pixel within `blur_size` goes black, in the default hexagon shape.
  Physical DOF (negative transitions) always uses the 32 px cap, so the blob is larger there; non-physical, ~23 px.
- Fixes it: `ALPHA = pow(clamp(v_coverage, 0.0, 1.0), coverage_exponent) * ...` — clean at all 42 positions of a sweep
  (distance 4.70–4.90 m, height 1.55–1.75 m, 1 cm steps) where the unpatched shader failed at 3.
- Doesn't fix it: vertex-only changes (raising the `side_length > 1e-5` threshold, guarding `width` / `drawn`). Note the
  vertex stage already clamps `v_coverage = clamp(width / drawn, 0.0, 1.0)`, so a negative value would have to come
  from interpolation.
- Unexplained: every probe reading `v_coverage`, `NORMAL` or `ALPHA` in the fragment also hides the fault, and no
  isnan/isinf/< 0 marker ever fired, so the exact bad value is unproven. Leading theory: perspective interpolation
  gives a slightly negative or −0 `v_coverage` at a triangle edge; `pow(x < 0, y)` is undefined in GLSL and NVIDIA lowers
  it to `exp2(y * log2(x))` = NaN. Alternative to rule out: a NaN from the anisotropic lighting on lashes pointing
  almost straight at the camera. To confirm: the compiled SPIR-V (and SASS if reachable), or a readback of the HDR color
  buffer at that pixel (a probe in the shader perturbs it; a readback does not).
- The eyebrows (and fuzz) use the same `HAIR_STRANDS` shader and are likely exposed too.

Repro (native): in the scratch copy,
`SCENE=res://scenes/camera_studio.tscn OUT=<dir> EMBED=1 STEPS="hide=AcknowledgementsPopup,DistanceSlider=4.8"
<editor> --path . --rendering-driver vulkan --resolution 1280x720 --script $PWD/tools/ui_harness.gd`, then look for the
hexagon in `*_DistanceSlider-4.8_vp.png`.

Overlap: task 25 (MetaHuman skin) also edits `metahuman_shaders.h` (the `SKIN` shader); keep your change to the strand
shaders so the merge stays clean.

## Goal

1. Find the actual NaN source in `HAIR_STRANDS`, with evidence, and check the other MetaHuman shaders for the same
   pattern (`pow` of an interpolated value, normalizing a possibly zero vector, anisotropy on a degenerate tangent).
   Fix it at the source, at minimum by clamping the interpolated coverage before `pow`.
2. Make the bokeh DOF passes robust to non-finite input, so one NaN pixel cannot blank a 32 px region: sanitize in
   `MODE_GEN_BLUR_SIZE`, or skip non-finite samples — pick the cheaper one that covers every DOF mode (bokeh hexagon and
   circle, box, physical and non-physical, Forward+ and Mobile's raster variant), and say what it costs.
3. Regression checks for both: a strand pointing straight at the camera (in `webgpu_tests/metahuman` or a standalone
   scene that needs no MetaHuman export), and a NaN pixel fed through the DOF passes (natively, and a Forward+ matrix
   arm or smoketest scene on the web).
4. Rebuild the editor and web templates in your worktree, re-import `scenes/Ava/head.dna` in the scratch copy, and
   confirm 4.80 m / 1.63 m renders cleanly with DOF on, natively and in a WebGPU export; rerun the user's 42-position
   sweep with the fix (and the DOF fix alone, with the strand fix reverted, to show it contains a NaN on its own).

## Verification

- The sweep's numbers before and after, native and web, in SUMMARY.md; frame pairs in `looks/`.
- The DOF arms of `run_forward_plus.mjs --parity` still pass (the sanitization must not change a finite image);
  `webgpu_notes/work/smoke.sh` passes; `webgpu_tests/metahuman/run.sh` passes.
