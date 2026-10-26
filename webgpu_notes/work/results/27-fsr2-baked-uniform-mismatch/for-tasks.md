# For webgpu_notes/TASKS.md (work task 27)

## §3 trap (next free number; 70 when this was written)

70. **The shader baker's capability override reached the editor's own frames** (2026-10-26, user report, work task
    27). A WebGPU export makes `RenderingDevice::has_feature()` answer as WebGPU (no image atomics, no FP16, ...) so the
    baked shaders get WebGPU's defines. The override was global and lasted the whole export, and the editor draws
    frames between the export's steps (every progress step does). So with FSR 2 on in the project (`scaling_3d/mode=2`),
    FSR 2's per-dispatch `has_feature(SUPPORTS_IMAGE_ATOMIC_32_BIT)` said "no" and bound storage buffers to the
    editor's image-atomics shaders: `Shader 'Fsr2ComputeLuminancePyramidPassShaderRD:0' Mismatch uniform type for binding
    (1), set (1). Expected 'Image', supplied: 'StorageBuffer'` (and the depth clip, reconstruct and lock passes), every
    editor frame of the export. **The log reads like a web runtime failure and is not one**: it sits between the
    export's "Shader baker:" lines, and the `:N` after the shader name is the variant, which proves the device: `:1` on
    the depth clip pass is the FFX_HALF variant, which only exists on an FP16 device (the Vulkan editor), and `:0` on the
    luminance pyramid is the image-atomics variant, which WebGPU never declares. Fix: the override applies only on a
    thread inside `RD::ShaderBakeFeatureOverrideScope`, opened by the baker's own callbacks. Every other per-frame
    `has_feature()` (fog, GI, VRS, FP16, depth resolve) had the same exposure without logging anything.
    Found on the way, same root: **FSR 2 built its variant list from `has_feature()` once, at construction**, with no
    refresh callback, so the bake baked the editor's flavor (FP16 + image atomics) under cache keys the WebGPU runtime
    never looks up, and dropped 7 of its stages as unconvertible: a baked web export with FSR 2 found none of its
    seven capability-split passes (all but RCAS) in the cache and compiled them from GLSL when FSR 2 was created (by
    construction of the cache key; the runtime log confirming it is in SUMMARY.md's pending list). A capability-dependent variant must be a shader group with
    `set_group_bake_requirements()` (fog.cpp, now fsr2.cpp) or refreshed by `add_general_defines_refresh_callback()`
    (fsr.cpp). `webgpu_tests/bake_isolation/run.sh` catches both.

## §4 archive entry (or a line under §4.23 / §2.27, the bake)

**Bake isolation (2026-10-26, work task 27).** User report: FSR 2 "Mismatch uniform type ... Expected 'Image',
supplied: 'StorageBuffer'" on every frame of a web export of camera-sim. Proven to be the *editor's* frames during the
bake, not the web runtime (trap 70). `RenderingDevice::has_feature()` now applies the bake's target answers only inside
`ShaderBakeFeatureOverrideScope` (per thread; `_begin_customize_resources`, `_customize_resource`,
`_customize_scene`). FSR 2's capability variants became groups with bake requirements (four groups for depth clip,
reconstruct and lock: atomics x FP16; two for the luminance pyramid; FP16 groups for accumulate, autogen reactive and
TCR autogen), only the selected one enabled, and `FSR2Effect::Device::use_image_atomics` records the answer the
resources and bindings follow. WGSL of the groups WebGPU enables is byte-identical to before (smoke: 276/276 identical
interface), so the precompiled table and stored references are unchanged. See SUMMARY.md for the numbers. New tier
`webgpu_tests/bake_isolation/run.sh`.

## Checked for the same split (capability-dependent shader text or variant count without a refresh)

- `fsr2.cpp`: the one found, fixed.
- `fsr.cpp` (half-float variant text): refresh callback since §2.27.
- `scene_shader_forward_clustered.cpp` SDF variant (`NO_IMAGE_ATOMICS`, `NEEDS_DUMMY_COLOR_ATTACHMENT`): refresh callback.
- `gi.cpp` SDFGI defines (`SUPPORTS_SHAREABLE_TEXTURE_FORMATS`): refresh callback.
- `copy_effects.cpp` raster octmap shaders (storage image limit): bake-only versions via its refresh callback.
- `fog.cpp`: groups with bake requirements.
- `scene_shader_forward_mobile.cpp` FP16: groups with bake requirements.
- `cluster_builder_rd.cpp` (`USE_ATTACHMENT`): both variants always declared, only the selection depends on the device.
- `emulate_point_size` (forward clustered and mobile): a specialization constant, not text.
- `gi.cpp:499`, `fog.cpp:477`, `render_scene_buffers_rd.cpp`, `render_forward_mobile.cpp` depth resolve,
  `texture_storage.cpp` VRS, `render_forward_clustered.cpp` side-effects attachments: runtime decisions, not shader
  text. They were exposed to the leaked override in the editor during a bake and are covered by the scope fix.
- `modules/`: no `RD::has_feature()` users.

## Questions for the user

- None that block. One for awareness: the user's camera-sim `project.godot` was edited at 16:56 today and no longer has
  `rendering/scaling_3d/mode=2` (FSR 2), presumably in reaction to these errors. With this fix FSR 2 can go back on.
  Recommended default: re-enable it after the merged editor is installed (it was the user's own setting).
