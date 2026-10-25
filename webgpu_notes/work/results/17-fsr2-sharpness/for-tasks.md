# Task 17: for TASKS.md

## §4 (archive): FSR2 on WebGPU now at native sharpness

Fixed by task 17. FSR2 read its reactive mask through an alpha-swizzled view. WebGPU cannot swizzle, so the
reactivity was the scene's red channel. fsr2_1 Laplacian went from 0.83x native to 1.000x, and the mean difference from
0.55 to 0.14. FSR2 now reads the alpha itself (amd-fsr2 patch 0005). Details: results/17-fsr2-sharpness/SUMMARY.md.

## §3 trap: texture view swizzles are silently dropped on WebGPU

See for-docs.md. Symptom: an effect that reads a swizzled view gets the unswizzled channel, with no error.

## §3 trap: FSR2 jitter has 32 phases at scale 0.5, not 16

FSR2's jitter phase count is `8 / scale^2`, so 32 at the Forward+ matrix's 0.5. Aligning captures mod 16 left the
FSR2 arm on one of two phases at random (0.55 / 0.70 from native, before the fix). The fixture now aligns mod 32.

## Open (small, not done here)

- `drivers/webgpu/SCsub`: the precompiled WGSL table depends on `servers/rendering/renderer_rd/shaders/**/*.glsl` but
  not on the headers they include from `thirdparty/` (amd-fsr2 shaders). A change confined to those headers leaves a
  stale table. It is keyed by SPIR-V hash, so it misses rather than serving wrong WGSL. Adding
  `thirdparty/amd-fsr2/shaders/*.h` to `shader_sources` would close it.
- FSR2's accumulate pass binds `r_input_depth` (set 1 binding 5) as a float texture, and the driver substitutes the
  blank fallback for it every frame (verbose log: `'Fsr2AccumulatePassShaderRD:1' set 1 binding 5: a 320x180 depth
  texture is bound where the shader declares a float texture`). It is read only to derive motion vectors for pixels
  without them (Godot's `GODOT_DERIVE_INVALID_MOTION_VECTORS`, i.e. the sky). Harmless if the fallback reads 0, the sky's
  reversed-Z depth, so no visible effect in the fixture. Worth a check under camera motion with the sky in view.
  `SceneForwardClusteredShaderRD` set 1 binding 24 gets the same message (4x4 depth texture).

## Questions for the user

- FSR2 look (default: accept): `fsr2_before_native_left_web_right.png` / `fsr2_after_native_left_web_right.png` in
  results/17-fsr2-sharpness/. Native Vulkan left, WebGPU right. Does the "after" pair match?
