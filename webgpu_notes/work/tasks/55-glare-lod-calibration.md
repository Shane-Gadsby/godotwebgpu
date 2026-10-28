# 55-glare-lod-calibration: exact glare blur sizes at every resolution

State: ready-for-integration
Owner: worker-55
Claimed: 2026-10-28 14:39
Needs: build (editor + web template), gpu (native renders, one WebGPU check)
Branch: work/55
Results: webgpu_notes/work/results/55-glare-lod-calibration/

User answer, 2026-10-28 (hand-offs `glare-lod-calibration`: **Recalibrate**).

## The problem

The camera glare chain (task 35, `_render_camera_glare_chain()`; read by `camera_glare_inc.glsl`'s `camera_glare_lod()`)
picks a mip level for a blur of sigma frame heights with `lod = log2(sigma * glare[5].x)`. That assumes lod 0 blurs by
a fixed sigma in texels (2.68, calibrated at 720p by task 35) and that each lod doubles it. Task 40 measured, from HDR
captures (§3 trap 80), that neither holds: lod 0 of the half-resolution chain is about 2.0 texels, and the growth per
lod is not exactly 2. At 720p the errors cancel; at 1080p halation (radius >= 1), the diffusion filter and the lens
flare blur all come out about 10% small, and the high-quality chain's floor is 1.91 px against a 2.52 px model at
radius 0.5. Numbers: `results/40-camera-answers-wave2/for-tasks.md` and `numbers.txt`.

## Goal

Calibrate per lod from measured sigmas, so halation, diffusion and flare blur sizes match their model at every
resolution, in both the standard (half-resolution) and the high-quality (full-resolution, `halation_high_quality`)
chains, in Forward+, Mobile and the web.

- Measure each lod's effective Gaussian sigma (in texels of that lod and in frame heights) for both chains, at several
  heights (at least 720, 1080, 1440 and 2160 lines, plus an odd one such as 900), from HDR captures. Fit, don't
  eyeball: task 40's `tools/analyze_halo.py` fits f (G_sigma * S - S).
- Replace the single `s 2^L` model with what the measurements support: a per-lod table of cumulative sigma (and
  interpolation between lods by sigma, not by lod index), or a corrected closed form if it fits within ~2% everywhere.
  Keep it in one place that `camera_attributes_storage.cpp` fills and the shader reads (the CameraEffects UBO `glare`
  block has free components; say which you take). Cite the measurement in a comment.
- Sizes below the chain's lod-0 floor stay at the floor (say so in the class docs, as now).
- **This changes existing glare looks by up to ~10% in size; that is the user's decision.** Default-off scenes must
  still render bit-identically (no glare enabled = no change).

## Verification

1. Halo size against the model for halation radius 0.5, 1, 4, 16 at every measured resolution, both chains: within
   ~3% from radius 1 up (report the table).
2. Diffusion filter and lens flare blur sizes the same way (one or two settings each).
3. Defaults bit-identical against the pre-change editor (3 renderers).
4. WebGPU (Chrome, launch rule) matches native Forward+ for one halation case.
5. smoke.sh; dev_mode editor build.

Read first: `webgpu_notes/work/README.md`, `camera-batch.md`, TASKS §4.54 and §4.58, §3 trap 80, task 35 and 40 results.
