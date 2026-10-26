# 24-white-balance: a White Balance attribute on CameraAttributesPhysical and CameraAttributesPractical

State: open
Owner: worker-24
Claimed:
Needs: build, gpu (web and parity captures)
Branch: work/24
Results: webgpu_notes/work/results/24-white-balance/

## Context

User request, 2026-10-26: "Camera physical & practical attribute 'White Balance'". Godot's `CameraAttributes` has
exposure, auto exposure and depth of field, but no white balance; the user's cameraSim project
(`~/Downloads/cameraSim_2026-09-09_10-26-06/testing`) simulates real cameras, where white balance is a basic setting.
Not part of the converter swap; run on the same board. An engine addition, so it also gets a wiki page
("Engine Additions" section, https://github.com/Shane-Gadsby/godotwebgpu/wiki; local clone `../godotwebgpu.wiki`).

## Goal

1. A white balance on both `CameraAttributesPhysical` and `CameraAttributesPractical` (the shared base
   `CameraAttributes` is fine if both inherit it; say which and why): a color temperature in Kelvin and a green-magenta
   tint, with camera semantics — the temperature is the illuminant the camera is set for, so a 6500 K default is
   neutral, 3200 K turns a daylight scene blue and makes a tungsten-lit one look neutral. Look at how Unreal
   (post-process `WhiteTemp` / `WhiteTint`) and Blender do it before choosing the model (a chromatic adaptation in a
   cone space, e.g. Bradford or CAT02, computed on the CPU as a 3x3 matrix); the doc text says which one.
2. Plumbed the way exposure is: properties, `RenderingServer` API, `RendererCameraAttributes`, applied in linear
   scene-referred color before tonemapping, in **Forward+, Mobile and Compatibility**. Defaults must change nothing:
   identical output (pixel diff 0) at 6500 K / tint 0, and no extra cost then if it can be skipped.
3. Works on the web (WebGPU Forward+, the real target). If the tonemap shader's push constants or uniforms change,
   the stored interface references (`webgpu_tests/spirv_cross_eval/interface_reference.json`,
   `webgpu_tests/wgsl_text_scan/scan_reference.txt`) need regenerating; do that deliberately and say why in the commit.
4. Docs: `doc/classes` XML for every new property and RS method; a Forward+ matrix arm for it
   (`webgpu_tests/forward_plus`, so `--parity` checks it against native Vulkan); and in `for-docs.md` a wiki page draft
   for Engine Additions plus the TASKS.md §4 entry text.

## Verification

- Native editor builds (`dev_build=yes`; also `local_ci.sh --dev-mode`'s warning flags on the changed files).
- A scratch scene with a neutral grey card and a white light: 6500 K bit-identical to before the change; 3200 K and
  10000 K move the card's RGB the way the chosen model predicts (numbers in SUMMARY.md); the same on Vulkan Forward+,
  Mobile, Compatibility and the WebGPU export.
- `run_forward_plus.mjs --parity --feature white_balance` (and a few neighbours) passes under the `gpu` lease;
  `webgpu_notes/work/smoke.sh` passes.
- Frame pairs (3200 / 6500 / 10000 K, native beside WebGPU) in `results/24-white-balance/looks/` for the hand-offs page.
