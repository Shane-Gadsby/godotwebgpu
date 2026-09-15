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
// binding 3..10 (WebGPU) or one `texture3D my_array[8]` at binding 3 (every
// other backend, unaffected -- plain GLSL array, no workaround needed) and,
// either way, a `my_array_sample(uint idx, sampler samp, vec3 uv, float lod) -> vec4`
// function. Call `my_array_sample(idx, samp, uv, lod)` instead of
// `textureLod(sampler3D(my_array[idx], samp), uv, lod)` (or `texture(...)` ->
// pass `0.0` for lod) at every access site; the call-site syntax is identical
// on every backend. Since this declares 8 consecutive bindings on WebGPU,
// place the macro invocation last among a binding set's declarations (or
// otherwise leave 7 binding numbers free after it) to avoid colliding with
// whatever comes next.
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

#ifdef RENDER_DRIVER_WEBGPU

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

#else

#define WEBGPU_DECLARE_TEXTURE3D_ARRAY8(m_name, m_binding0)                    \
	layout(set = 0, binding = (m_binding0)) uniform texture3D m_name[8];       \
	vec4 m_name##_sample(uint m_idx, sampler m_samp, vec3 m_uv, float m_lod) { \
		return textureLod(sampler3D(m_name[m_idx], m_samp), m_uv, m_lod);      \
	}

#endif
