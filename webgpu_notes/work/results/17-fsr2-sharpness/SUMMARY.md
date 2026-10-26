# Task 17: FSR2 softer on WebGPU, 2026-10-25 (worker-17)

## Cause: FSR2's reactive mask was the scene's red channel on WebGPU

`render_forward_clustered.cpp` handed FSR2 `RenderSceneBuffersRD::get_internal_texture_reactive()`: the color buffer
seen through a view that swizzles alpha into every channel. WebGPU has no texture view swizzle, and the driver drops it
without a word, so FSR2 read **red** as reactivity. Every lit pixel counted as reactive, and FSR2 widens its
upsampling kernel and trusts the history less where reactivity is high. Hence the softer, slightly "pixelated" look.
FSR2's dilated reactive mask (resource 28), shown on screen: native all 0, web mean 114/255, a red copy of the scene.

## Fix

- `thirdparty/amd-fsr2/shaders/ffx_fsr2_callbacks_glsl.h`: new Godot option
  `FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_FROM_ALPHA`. `LoadReactiveMask()` then reads `.a` of the bound texture instead of
  `.r`, before the existing 0.9 clamp. Patch `thirdparty/amd-fsr2/patches/0005-godot-reactive-mask-from-alpha.patch`.
- `effects/fsr2.cpp` defines that option. `render_forward_clustered.cpp` passes the plain color slice
  (`get_internal_texture(v)`). `fsr2.h` says what `Parameters::reactive` must hold.
- Same on every backend: `.a` of the plain view is `.r` of the alpha-swizzled one. **The native frame is byte-identical
  before and after the fix.** `get_internal_texture_reactive()` stays, because MetalFX uses it and Metal swizzles.
- Fixture: `JITTER_PERIOD` 16 -> 32 (`fp_scene.gd`, README). FSR2 at scale 0.5 cycles 32 jitter phases
  (`8 / scale^2`), so with 16 the web captured FSR2 on phase 0 or 16 at random (native always 0). That moved fsr2 between
  0.55 and 0.70 before the fix, and between 0.14 and 0.17 after it. 32 is a multiple of fog's and TAA's 16.
- `features.mjs`: fsr2 `parityTol` 0.8 -> 0.3. The old comment blamed fp16 and the SNORM tables, which is wrong (below).

## Measurements

4080 SUPER, system Chrome headed, `--baked --parity`, fsr2_1 frame. Metrics: 3x3 Laplacian sigma of luma (`sharpness.py`)
and mean |diff| vs native (0..255).

| | native Lap | web Lap | web/native | mean diff |
|---|---|---|---|---|
| before, 62a570d31f pair, phase-aligned (capture frame 480), 4 runs | 13.34 | 11.03 | 0.826 | 0.546 |
| before, capture landed on the other FSR2 phase (496) | 13.34 | 11.84 | 0.888 | 0.698 |
| before, Firefox (wgpu), frame 496 | 13.34 | 11.83 | 0.887 | 0.698 |
| **after, this branch's pair, 3 runs (full matrix + 2 single)** | **13.34** | **13.35** | **1.000** | **0.142** |
| TAA, before and after | 12.44 | 12.44 | 1.000 | 0.006 |
| scaling_3d (bilinear) | 7.63 | 7.63 | 1.000 | 0.002 |

The verification bar was "within 2%, well under 0.51". The result is +0.1% and 0.142. The 0.142 that remains is noise
of 1-2 levels on 29% of pixels, with 0.24% of pixels more than 2 off (max 32, at edges).

**Step 1 (re-measure on aligned captures) did not close the gap.** Native is deterministic. The phase-aligned web capture
reproduced the old 11.04 / 0.51 exactly, so the capture phase was not the cause here, unlike fog's in task 16.

What was ruled out, with a diagnostic native editor (env switches, not committed):
- **fp16 is not the cause.** Native without FFX_HALF is bit-identical, and so is native with the NO_IMAGE_ATOMICS buffer
  path. Native with WebGPU's R16F tables (in place of R16_SNORM) is within 0.000.
- **Not RCAS:** the gap is the same with sharpening off (`fsr_sharpness` 2.0: 0.864, 0.458) and at maximum (0.0: 0.895).
- **Not jitter sign or scale:** native with the jitter negated or zeroed gets sharper and aliased (Laplacian 19-29),
  not softer.
- **Not a Chrome/Dawn quirk:** Firefox renders the same frame.
- **How it was found:** a diagnostic `--fsr2-diag-show=<FFX resource id>` (web cmdline / native env) copied FSR2's
  internal resources onto the output. Prepared input color, lock status, new locks and dilated depth matched native.
  The dilated reactive masks did not.

Full matrix with the fixed pair, SPIRV-Cross-baked, `--parity`: **71/71 features pass**. `RESULT: FAIL` comes only from
the bake gate (translated 97 > 80), which predates this task (task 07).

Smoke (`webgpu_notes/work/smoke.sh`, this tree):
```
validation: 276 modules, 0 invalid, 0 of them expected
SMOKE OK (smoke-20261025-131239-2290400)
```

## For the user

`fsr2_before_native_left_web_right.png` and `fsr2_after_native_left_web_right.png`: the checker sphere and arch at 3x,
native Vulkan left, WebGPU right. For the hand-offs page as an FSR2 look.

## What the lead must still run / risks

- The editor and templates must be rebuilt together. The fix is in an engine shader (FSR2 GLSL) and in C++. My pair was
  built from this branch: `build-linux.sh` editor + web release nothreads, `wgsl_precompiled.gen.h` deleted first.
- **`wgsl_precompiled.gen.h` does not re-bake when only `thirdparty/amd-fsr2/shaders/*.h` changes.** SCsub's
  dependencies are the `.glsl` files under `servers/rendering/renderer_rd/shaders` only, not the headers they include
  (see for-tasks.md). The table is keyed by SPIR-V hash, so a stale table only misses: those stages are converted at
  runtime unless the export bake covers them, which it does for FSR2. Delete the header, or run
  `--force-shader-rebake`, when merging.
- Saved `--reference` directories from before the `JITTER_PERIOD` change are on other frames. Re-render them.
  Each config now waits up to 31 frames instead of 15, about 0.25 s more per arm.
