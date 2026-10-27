# 36-camera-exposure-automation: auto exposure metering modes, auto white balance

State: claimed
Owner: worker-36
Claimed: 2026-10-28 (worker-36)
Needs: build, gpu, task 33 (the CameraEffects UBO exposure region)
Branch: work/36
Results: webgpu_notes/work/results/36-camera-exposure-automation/

Read `webgpu_notes/work/camera-batch.md` first.

## Goal

1. **Metering modes** for auto exposure (CameraAttributes, both kinds): Average (today's behavior, default),
   Center-weighted (e.g. 60-80% weight in the central circle of 1/4 frame — cite a camera maker's definition), Spot
   (~2-5% of frame at the center or at a normalized point), Highlight-weighted (protect highlights: meter so a high
   percentile sits below clipping). Works in the luminance reduction (`renderer_rd/effects/luminance.*` and its
   shaders), log-average luminance, with weights; Forward+ (auto exposure is Forward+ only today; Mobile/Compat stay
   unsupported, as documented).
2. **Auto white balance** (CameraAttributes): estimate the illuminant from the frame (gray world with highlight/
   saturation rejection, or white patch; cite the algorithm), adapt over time (speed), clamp to a temperature range, and
   feed the same Bradford cone gains white balance uses (§4.42) — on the GPU (a small reduction + a 1x1 gains texture
   or UBO region the tonemapper reads), since the CPU must not stall on readback. Manual `white_balance_temperature`/
   `tint` become the starting point/bias. Mobile and Compatibility: if a reduction is not available, document it.

## Verification

Default bit-identical. Metering: a scene with a bright window region and a dark interior; exposure value reached per
mode compared with a reference computed from the frame in Python. AWB: a scene lit by a 3200 K light under 6500 K
manual white balance converges toward neutral gray (measure the gray card's chromaticity over frames). WebGPU vs
native, smoke.
