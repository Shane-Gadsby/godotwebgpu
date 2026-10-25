# Task 16: the baked volumetric-fog "race", 2026-10-25 (worker-16)

## Cause: the harness, not the engine. The capture frame's jitter phase was not fixed.

Volumetric fog jitters its froxel sample position by `halton_map[frame_number % 16]`
(`fog.cpp`, `VolumetricFog::MAX_TEMPORAL_FRAMES = 16`). A settled, temporally reprojected fog still differs from
one phase to the next. The fixture captured frame `boot_offset + settle (+ settle ...)`, and `boot_offset` came from
whether the harness's first `location.hash` write landed before the fixture's first or second `_process()`. So the
capture fell on phase 0 or phase 1, at random. A baked export boots fast enough to straddle that frame. An unbaked
one converts shaders during its first frames, so the hash was always there by the first process step, which is why
unbaked runs always passed.

Evidence:
- **Perfect correlation on the web** (fixture instrumented to print `Engine.get_frames_drawn()` at READY, SPIRV-Cross-baked,
  old fixture): 7 runs. Capture frame 240/480 (phase 0) gave 0.046 (pass) 3 of 3 times; 241/481 (phase 1) gave
  0.082 (fail) 4 of 4 times. The native reference had been captured at 239/479 (phase 15).
- **Reproduced natively, no browser**: native Vulkan against itself, with the reference at phase 15, phase 0 measures
  **0.0453** and phase 1 **0.0823**. Those are the two "exact states" the web reported. Phase 0 against phase 1 is
  0.0717. So WebGPU was at parity all along; the 0.046 "pass" was itself a one-step phase mismatch.

## Fix (fixture only, no engine change, no rebuild)

`webgpu_tests/forward_plus/godot/fp_check/fp_scene.gd`: each config is applied in the process step of the frame
that makes its capture land on phase `_phase` (0) mod 16 (`_aligned()` / `_spec_aligned()`). On the web the hash
request is queued, and the render loop is re-enabled until that frame comes. Natively the matrix waits the same way,
then counts `settle - 1` more frames, so both sides follow the same timeline: N frames drawn with the new config,
the capture being the last. Every config still settles the same number of frames, so particles simulate the same
time. `--fp-phase=N` (native) is there for triage, and `[FP] FRAME <n> (phase <p>)` is printed before `READY` on
both sides. The forward_plus README has a fourth item under "Getting a comparable frame".

## Results (4080 SUPER, system Chrome headed, editor + web release nothreads template from 62a570d31f, copies)

| export | runs | volumetric_fog native diff (on) |
|---|---|---|
| SPIRV-Cross-baked, old fixture | 7 | 0.046 x3 (phase 0), 0.082 x4 (phase 1) |
| **SPIRV-Cross-baked, fixed** | **10** | **0.001 x10**, every capture at phase 0 (frames 240/480 or 256/496) |
| **Tint-baked (`tint_convert_cli.tint`), fixed** | **10** | **0.001 x10** |

The fog arm is now 0.001 from native, down from 0.046 on the luckier phase. The Tint-baked pack really holds Tint
WGSL: it contains `tint_` identifiers and the SPIRV-Cross pack has none.

**Full matrix, SPIRV-Cross-baked, `--baked --parity` (native reference rendered by the fixed fixture): 71/71
features pass.** For comparison, taa 0.006, fsr2 0.546 (task 17). `RESULT: FAIL` comes only from the bake gate
(`translated 97 > 80`), which predates the swap and is described in task 07's SUMMARY. Logs: `runs.txt`,
`full_matrix_baked_spirv_cross.txt`.

Smoke (`webgpu_notes/work/smoke.sh`, this tree):
```
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-121656-1699987)
```

## What the lead must still run / risks

- Any saved `--reference DIR` made before this fix is at phase 15 and is now invalid. Re-render it (omit `--reference`).
- A capture now waits up to 15 extra frames, about 0.25 s per arm. TAA, FSR2 and the other jitter users also land on a
  fixed phase now. That can only make them more stable, but their numbers may move slightly: fsr2 measured 0.546
  here.
- I did not run the full matrix with the Tint-baked or unbaked exports; only the fog arm was run 10 times with
  Tint-baked.
