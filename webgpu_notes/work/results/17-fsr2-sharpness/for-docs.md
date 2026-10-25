# Task 17: lines for the shared docs

## thirdparty/README.md, amd-fsr2 "Patches:" list (after 0004)

- `0005-godot-reactive-mask-from-alpha.patch` (Godot WebGPU fork only:
  `FFX_FSR2_OPTION_GODOT_REACTIVE_MASK_FROM_ALPHA` makes `LoadReactiveMask()` read the
  reactivity from the alpha of the color buffer Godot binds, instead of `.r` through
  an alpha-swizzled view, since WebGPU texture views cannot swizzle)

## features.mjs (fsr2 `parityTol` comment) -- done on the branch, see SUMMARY.md

## drivers/webgpu/README.md or TASKS.md §3 (a trap)

**Texture view swizzles are silently dropped on WebGPU.** `RD::texture_create_shared()` with a
`TextureView` whose `swizzle_*` is not identity creates a plain view: WebGPU has no component
swizzle, and the driver stores the swizzle without applying it (rendering_device_driver_webgpu.cpp,
"WebGPU has no texture component swizzle"). No error, no warning. FSR2's reactive mask was such a
view (`RenderSceneBuffersRD::get_internal_texture_reactive()`, alpha broadcast to RGBA), so on the
web FSR2 read the scene's red channel as reactivity, and every lit pixel was reactive: softer
output, 0.55 from native. Any new code that relies on a swizzled view needs the swizzle done in
the shader instead. `get_internal_texture_reactive()` itself remains (MetalFX uses it, Metal
honors swizzles).
