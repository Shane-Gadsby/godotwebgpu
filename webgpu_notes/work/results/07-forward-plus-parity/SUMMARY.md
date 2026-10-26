# Task 07: Forward+ parity against native Vulkan, 2026-10-25

Fixture `webgpu_tests/forward_plus/godot/fp_check`, `run_forward_plus.mjs --parity`, 4080 SUPER, system Chrome headed.

| reading | editor + template | shaders | result |
|---|---|---|---|
| baseline | 2026-10-23 pair | baked by Tint (`tint_convert_cli.tint-pre-swap`), ~97 stages converted at runtime by the template's Tint | 71/71 features; bake gate fails (translated 97 > 80) |
| first SPIRV-Cross | 2026-10-23 pair | baked by the 09:35 SPIRV-Cross converter (no patches 0001/0006/0015), ~99 runtime-Tint | 0/71: the contact-shadow module errors on every page load; frames: 133/142 within 0.1/255 of Tint's, ambient_light/fxaa/contact_shadows black, SSR without reflections (old driver scans) |
| **full, unbaked** | **62a570d31f pair** | **every stage converted at runtime by SPIRV-Cross** | **71/71 PASS** |
| full, baked | 62a570d31f pair | baked by SPIRV-Cross, the same ~97 runtime stages via SPIRV-Cross | 70/71: volumetric_fog flips; bake gate as baseline |

Volumetric fog in baked exports lands in one of two exact states per run: 0.046 from native (pass) or 0.082 (fail).
Not swap-caused: Tint-baked on the old pair fails 3 of 9 runs, Tint-baked on the new pair 1 of 4, SPIRV-Cross-baked 8 of
11, SPIRV-Cross unbaked 0 of 5. A longer settle (600 frames) does not remove it, and the shader stats are identical in
passing and failing runs: an existing race in the baked path → task 16. Whether SPIRV-Cross makes it more likely is
not decidable from these samples; fixing the race settles it.

The bake gate (`translated <= 80`) fails for every converter: 8 Forward+ scene-shader groups (×12) and one particles
stage are converted at runtime because the baker never sees them. Pre-existing, not a swap regression.

Frames for the user (Vulkan left, WebGPU right): FSR2, SSIL, SSAO, contact shadows, SSR, volumetric fog (passing and
failing), on the hand-offs page's Look strip.
