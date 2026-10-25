# 17-fsr2-sharpness: FSR2 on WebGPU is visibly softer than native Vulkan (pre-existing)

State: open (not a swap regression; found while judging task 07's frames)
Owner:
Claimed:
Needs: gpu
Branch: work/17
Results: webgpu_notes/work/results/17-fsr2-sharpness/

## Context

The user, judging the FSR2 look (2026-10-25): "the web version looks a little more pixelated but otherwise good".
Measured on fp_check's `fsr2_1` frame (3x3 Laplacian standard deviation, higher = more edge detail): native Vulkan
12.82; WebGPU 11.04 with the pre-swap Tint bake and 11.04 with SPIRV-Cross (identical frames, 0.51 mean difference
from native each); FSR2 off: 7.85 on all three. So WebGPU's FSR2 output differs from native in the same way under both
converters: within the parity tolerance (`run_forward_plus.mjs` passes it), but visible.

**Check first (from task 16, 2026-10-25):** the frames measured above were captured on different temporal-jitter
phases (native on phase 15, web on 0 or 1); FSR2's jitter cycles with the frame number like fog's. Task 16 made every
capture land on phase 0. Re-measure on aligned captures before looking for an engine cause: part or all of the gap may
have been the capture. After the fix the fixture still reports fsr2 0.546 from native.

## Goal

Find where the web FSR2 path differs from native: render scale / jitter sequence, RCAS sharpening (strength, the
`sharpness` push constant), the lock/reactive masks, half-float paths (`SUPPORTS_HALF_FLOAT`, TASKS.md §2.20), sampler
filtering on the upscale inputs (no LOD bias on WebGPU samplers: `apply_sampler_lod_bias`), or the
`no_image_atomics` variant used on WebGPU (task 01's module). Fix it so the frame matches native's sharpness.

## Verification

`fsr2_1` Laplacian within 2% of native and mean difference well under the current 0.51; the user's look on the
hand-offs page.
