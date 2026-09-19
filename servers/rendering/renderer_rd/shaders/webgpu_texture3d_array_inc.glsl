// WebGPU has no equivalent of Vulkan/Metal/D3D12's descriptor/argument arrays --
// a single shader binding can never represent more than one independently-
// selectable texture. GLSL's `uniform texture3D foo[8]` (an array of separate
// opaque handles, each getting its own descriptor slot, dynamically indexed at
// runtime via e.g. `foo[cascade_index]`) has no direct WGSL equivalent, and
// Tint cannot even parse SPIR-V containing such an array type regardless of
// size. This driver's SPIR-V preprocessing (`flatten_binding_arrays()`,
// drivers/webgpu/spirv_preprocess.cpp) works around Tint's inability to parse
// the type at all by collapsing it to a single scalar binding -- but that
// silently discards the runtime index, always resolving every access to
// whichever texture ends up bound at that one slot regardless of which array
// element was actually requested. See Task 9.5 Round 36/37 in
// webgpu_notes/TASKS.md for how this was found (the SDFGI brightness-runaway
// bug -- confirmed root cause via a direct experiment: changing which array
// element the driver binds changed the bug's behavior) and its broader scope
// (VoxelGI's and particles' collider textures use the identical pattern and
// are equally affected, not yet fixed).
//
// These macros replace a `uniform texture3D name[8]` declaration plus
// `textureLod(sampler3D(name[idx], samp), uv, lod)` access with 8
// individually-bound variables and a helper function that explicitly branches
// on the runtime index -- letting glslangValidator generate correct SPIR-V
// control flow for the branch itself, rather than needing a much larger and
// higher-risk SPIR-V-level control-flow-synthesis pass to reconstruct it
// after the fact from the collapsed form. Verified end-to-end
// (glslangValidator -> the full spirv_preprocess.cpp pass pipeline -> Tint ->
// WGSL) with a standalone fixture before use here.
//
// Uses textureLod (explicit LOD) rather than texture() so one helper serves
// every call site in this codebase: compute shaders have no automatic
// derivatives, so a plain texture(sampler3D(...)) call there is already
// exactly textureLod(..., 0.0) by the GLSL spec -- pass 0.0 at those call
// sites and it's identical to the original.
//
// Usage: WEBGPU_DECLARE_TEXTURE3D_ARRAY8(my_array, 3) declares 8 bindings at
// binding 3..10 and a `my_array_sample(uint idx, sampler samp, vec3 uv, float lod) -> vec4`
// function. Call `my_array_sample(idx, samp, uv, lod)` instead of
// `textureLod(sampler3D(my_array[idx], samp), uv, lod)` (or `texture(...)` ->
// pass `0.0` for lod) at every access site. Since this declares 8 consecutive
// bindings, place the macro invocation last among a binding set's
// declarations (or otherwise leave 7 binding numbers free after it) to avoid
// colliding with whatever comes next.
//
// IMPORTANT: never choose m_binding0 such that m_binding0..m_binding0+7
// includes 120. drivers/webgpu/spirv_preprocess.cpp's split_combined_samplers
// pass special-cases raw binding number 120 as PC_RING_BUFFER_BINDING (the
// push-constant emulation ring buffer's binding, set 3) and deliberately
// skips doubling it -- but that check only looks at the raw binding number,
// not the set, so a set-0 resource that happens to also sit at binding 120
// gets the same "don't double" treatment by mistake and collides with
// whatever real binding *did* get doubled into that slot. Cost a full
// confirmation-test cycle to find (Task 9.5 Round 37, webgpu_notes/TASKS.md).
//
// Always splits, on every backend -- NOT gated behind `#ifdef RENDER_DRIVER_WEBGPU`
// (removed 2026-09-20, see Task 24 round 3 in webgpu_notes/TASKS.md). That define
// reflects OS::get_current_rendering_driver_name() of the CURRENTLY RUNNING
// process, not the export target -- but export-time shader baking (the common,
// fast path for real projects) compiles this GLSL exactly once, from the native
// editor, which runs on Vulkan even when baking shaders FOR the WebGPU export
// target. So RENDER_DRIVER_WEBGPU was always false during baking, silently
// producing a real (unsplit) `texture3D[8]` in the baked SPIR-V -- which this
// driver's own uniform-array capability report (SUPPORTS_TEXTURE_ARRAY_BINDINGS
// = false, gi.cpp's _gi_bind_cascade_texture_array()) then mismatched against at
// bind-group-creation time: `GPUValidationError`/`ERR_FAIL` ("is an array of (8)
// textures... IDs provided: 1"), dropping the whole command buffer every frame
// SDFGI/VoxelGI ran. Splitting unconditionally means baked and live-compiled
// SPIR-V are always structurally identical, regardless of which driver happened
// to be active at compile time. Costs native backends 8 descriptor slots instead
// of 1 real array -- a real, working, if less elegant GLSL pattern -- in
// exchange for correctness independent of compile context.

#define WEBGPU_DECLARE_TEXTURE3D_ARRAY8(m_name, m_binding0)                    \
	layout(set = 0, binding = (m_binding0) + 0) uniform texture3D m_name##_0;  \
	layout(set = 0, binding = (m_binding0) + 1) uniform texture3D m_name##_1;  \
	layout(set = 0, binding = (m_binding0) + 2) uniform texture3D m_name##_2;  \
	layout(set = 0, binding = (m_binding0) + 3) uniform texture3D m_name##_3;  \
	layout(set = 0, binding = (m_binding0) + 4) uniform texture3D m_name##_4;  \
	layout(set = 0, binding = (m_binding0) + 5) uniform texture3D m_name##_5;  \
	layout(set = 0, binding = (m_binding0) + 6) uniform texture3D m_name##_6;  \
	layout(set = 0, binding = (m_binding0) + 7) uniform texture3D m_name##_7;  \
	vec4 m_name##_sample(uint m_idx, sampler m_samp, vec3 m_uv, float m_lod) { \
		if (m_idx == 0u) {                                                     \
			return textureLod(sampler3D(m_name##_0, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 1u) {                                                     \
			return textureLod(sampler3D(m_name##_1, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 2u) {                                                     \
			return textureLod(sampler3D(m_name##_2, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 3u) {                                                     \
			return textureLod(sampler3D(m_name##_3, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 4u) {                                                     \
			return textureLod(sampler3D(m_name##_4, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 5u) {                                                     \
			return textureLod(sampler3D(m_name##_5, m_samp), m_uv, m_lod);     \
		}                                                                      \
		if (m_idx == 6u) {                                                     \
			return textureLod(sampler3D(m_name##_6, m_samp), m_uv, m_lod);     \
		}                                                                      \
		return textureLod(sampler3D(m_name##_7, m_samp), m_uv, m_lod);         \
	}
