/**************************************************************************/
/*  webgpu_objects.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef WEBGPU_ENABLED

#include "servers/rendering/rendering_device_driver.h"

#include <webgpu/webgpu.h>

using RDD = RenderingDeviceDriver;

// =============================================================================
// Buffer
// =============================================================================

struct WGBuffer {
	WGPUBuffer handle = nullptr;
	uint64_t size = 0;
	WGPUBufferUsage usage = 0;
	uint8_t *shadow_map = nullptr; // For CPU-side mapping emulation.
	bool map_dirty = false;
	bool is_readback = false; // True for staging buffers that need GPU→CPU readback.
	bool map_complete = false; // Set by async map callback.
	bool map_pending = false; // True while wgpuBufferMapAsync callback is in flight.
	bool freed = false; // Source freed while map pending; callback will clean up.

	// Dirty range tracking for shadow buffer flushes. On WebGPU, buffer_unmap()
	// must copy shadow_map data to the GPU buffer via wgpuQueueWriteBuffer. Without
	// range tracking, the entire buffer (often 32MB for staging) is copied even when
	// only a few bytes were written — costing ~10ms per unmap on web. These fields
	// limit the flush to the actually-modified region.
	uint64_t dirty_offset = 0;
	uint64_t dirty_end = 0; // Exclusive end. 0 means "no explicit range set."

	// Staging-block writes deferred by command_copy_buffer(): the union of the regions copied out of this buffer this
	// frame, uploaded once by _flush_pending_staging() just before the next queue submit (one writeBuffer per block
	// instead of one per region).
	uint64_t pending_lo = UINT64_MAX;
	uint64_t pending_hi = 0;
	bool pending_listed = false;

	// Dynamic persistent rotation (Task 7.5).
	// `frame_idx` is UINT32_MAX for non-dynamic buffers. For BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT
	// buffers, buffer_create sets frame_idx=0 and per_frame_size to the aligned single-frame
	// slice. The physical buffer holds frame_count slices; each frame the CPU writes to one
	// slice and the GPU reads from it via a dynamic offset of frame_idx * per_frame_size.
	uint32_t frame_idx = UINT32_MAX;
	uint64_t per_frame_size = 0;
	bool is_dynamic() const { return frame_idx != UINT32_MAX; }
};

// =============================================================================
// Texture
// =============================================================================

struct WGTexture {
	WGPUTexture handle = nullptr; // null for shared/sliced views (they don't own the GPU texture)
	WGPUTexture view_source = nullptr; // always the owning WGPUTexture; inherited by shared/sliced textures
	WGPUTextureView default_view = nullptr;

	// Returns the underlying WGPUTexture for any GPU op that needs the actual
	// resource (copies, debug labels). Shared / sliced views have handle==nullptr
	// and must read view_source instead — passing nullptr to wgpu* functions
	// causes "Required member is undefined" / dispatch crashes in emscripten.
	// Use this for *any* WGPUTexture read into a Copy/CommandEncoder/etc.
	// Lifecycle paths (creation, release) still touch handle directly so they
	// only operate on owning textures.
	inline WGPUTexture gpu_handle() const {
		DEV_ASSERT(view_source || handle);
		return view_source ? view_source : handle;
	}
	uint32_t debug_create_id = UINT32_MAX; // Monotonic counter for correlating [WGTEX] log lines.
	WGPUTextureFormat format = WGPUTextureFormat_Undefined;
	// Godot-side DataFormat remembered at creation so readback / allocation-size
	// paths can look up true bytes-per-pixel instead of hardcoding bpp=4.
	RDD::DataFormat rd_format = RDD::DATA_FORMAT_MAX;
	WGPUTextureDimension dimension = WGPUTextureDimension_2D;
	WGPUTextureViewDimension view_dimension = WGPUTextureViewDimension_2D;
	uint32_t width = 0, height = 0, depth = 1;
	uint32_t mipmaps = 1;
	uint32_t layers = 1;
	uint32_t sample_count = 1;
	// For slice views (texture_create_shared_from_slice), the base offset of
	// this view into the parent texture. 0/0 for non-slice textures.
	uint32_t base_mipmap = 0;
	uint32_t base_layer = 0;
	WGPUTextureUsage usage = 0;
	bool is_from_swap_chain = false;
};

// =============================================================================
// Sampler (stored directly as WGPUSampler handle in SamplerID)
// =============================================================================

// =============================================================================
// Vertex Format
// =============================================================================

struct WGVertexFormat {
	struct Attribute {
		uint32_t location = 0;
		WGPUVertexFormat format = (WGPUVertexFormat)0; // 0 = no valid format (Undefined was removed in Dawn)
		uint64_t offset = 0;
		uint32_t binding = 0;
	};
	struct Binding {
		uint32_t stride = 0;
		WGPUVertexStepMode step_mode = WGPUVertexStepMode_Vertex;
	};
	LocalVector<Attribute> attributes;
	LocalVector<Binding> bindings;
};

// =============================================================================
// Shader
// =============================================================================

struct WGShader {
	// Per-stage shader modules. Indexed by RDD::ShaderStage enum value.
	// Created on first use by a pipeline, not when the shader is: until then
	// stage_wgsl holds the stage's final WGSL (see
	// RenderingDeviceDriverWebGPU::_get_stage_module()). Read them through that.
	WGPUShaderModule stage_modules[6] = {}; // SHADER_STAGE_MAX = 6 (vertex/frag/tess×2/compute/max)
	CharString stage_wgsl[6];
	WGPUShaderModule module = nullptr; // Legacy alias — points to first non-null module.

	// Per-stage raw SPIR-V bytes. Stored for deferred specialization constant patching.
	PackedByteArray stage_spirv[6];

	WGPUPipelineLayout pipeline_layout = nullptr;
	LocalVector<WGPUBindGroupLayout> bind_group_layouts; // One per descriptor set.

	// Push constant emulation.
	uint32_t push_constant_size = 0;
	uint32_t push_constant_bind_group = UINT32_MAX; // UINT32_MAX = no push constants.
	uint32_t push_constant_binding = 0;
	BitField<RDD::ShaderStage> push_constant_stages;
	// Merged bind group layout for the PC group when it also has material uniforms.
	// nullptr = PC group has no other uniforms; pipeline uses the universal PC-only layout.
	WGPUBindGroupLayout merged_pc_group_layout = nullptr;

	// Reflection data for uniform set creation:
	//   bind_group_infos[set_index].entries[binding_index] → layout entry + Godot type.
	struct BindGroupEntry {
		WGPUBindGroupLayoutEntry layout_entry = {};
		RDD::UniformType godot_type = RDD::UNIFORM_TYPE_MAX;
		uint32_t array_length = 0; // >1 for binding_array (e.g., texture arrays)
		// UNIFORM_TYPE_SAMPLER_WITH_TEXTURE combined bindings occupy TWO raw
		// WebGPU binding slots (sampler at *2+0, texture at *2+1), but only
		// one BindGroupEntry is recorded per Godot uniform above -- with
		// layout_entry set to the TEXTURE half. Stash the sampler half here
		// too, so uniform_set_create() can recover its (Non)Filtering type
		// (needed e.g. to substitute a NonFiltering sampler for a real
		// Filtering one paired with a depth texture -- see
		// dummy_nonfiltering_sampler / Task 8.9, Task 7.13).
		WGPUBindGroupLayoutEntry paired_sampler_entry = {};
		bool has_paired_sampler_entry = false;
		// UNIFORM_TYPE_SAMPLER used by both a depth texture and another texture (see sampler_depth_use_shared_check).
		bool sampler_shared_with_non_depth = false;
	};
	struct BindGroupInfo {
		LocalVector<BindGroupEntry> entries;
	};
	LocalVector<BindGroupInfo> bind_group_infos;

	// Read-write storage texture splits: maps (set << 16 | write_binding) → read_shadow_binding.
	// When readonly-and-readwrite-storage-textures is unavailable, read_write storage
	// textures are split into separate write (original) + read (shadow) bindings.
	// The shadow binding receives a GPU copy of the texture at bind group creation time.
	HashMap<uint32_t, uint32_t> rw_storage_splits;

	// Read-only storage texture → sampled texture conversions.
	// Set of (set << 16 | binding) keys. When readonly-and-readwrite-storage-textures
	// is unavailable, read-only storage textures are converted to sampled textures
	// in the WGSL and BGL so the shader compiles on Firefox/wgpu.
	HashSet<uint32_t> read_storage_to_sampled;

	// Pipeline layout gap indices — slots between set_count and pc_group that
	// need empty bind groups bound before draw (Firefox/wgpu requirement).
	LocalVector<uint32_t> gap_bind_group_indices;

	// The exact per-group layout array handed to wgpuDeviceCreatePipelineLayout:
	// bind_group_layouts[i] for a populated set, merged_pc_group_layout or the
	// universal push_constant_bind_group_layout at the PC group, and
	// empty_bind_group_layout at every gap and every set that has no uniforms.
	//
	// This -- not bind_group_layouts -- is what Dawn validates each bound bind
	// group against at draw time, for *every* group in the layout, including
	// ones the shader never reads (where Vulkan only cares about the sets a
	// pipeline actually accesses). Kept so the driver can check that invariant
	// itself: see _reconcile_render_bind_groups(). TASKS.md §2.1.
	LocalVector<WGPUBindGroupLayout> pipeline_group_layouts;

	String name;

	// True if the base shader modules were compiled with WGSL override
	// declarations (spirv_to_wgsl_with_overrides). When set, specialization
	// constants are passed as WGPUConstantEntry at pipeline creation time
	// instead of patching SPIR-V + runtime Tint conversion.
	bool has_override_declarations = false;

	// Per-stage set of override constant IDs found in the WGSL output.
	// Used to filter WGPUConstantEntry per stage — WebGPU requires that
	// every constant key passed to a stage actually exists in that module.
	HashSet<uint32_t> stage_override_ids[6];
};

// =============================================================================
// Render Pass (metadata — no WebGPU object)
// =============================================================================

struct WGRenderPass {
	struct SubpassInfo {
		LocalVector<RDD::AttachmentReference> input_references;
		LocalVector<RDD::AttachmentReference> color_references;
		RDD::AttachmentReference depth_stencil_reference;
		LocalVector<RDD::AttachmentReference> resolve_references;
	};
	LocalVector<RDD::Attachment> attachments;
	LocalVector<SubpassInfo> subpasses;
	uint32_t view_count = 0;
	bool is_swap_chain_pass = false; // True for swap chain render passes — used to strip alpha writes.
};

// =============================================================================
// Framebuffer (metadata — no WebGPU object)
// =============================================================================

struct WGFramebuffer {
	LocalVector<WGTexture *> attachments;
	LocalVector<WGPUTextureView> attachment_views;
	uint32_t width = 0, height = 0;
	WGRenderPass *render_pass = nullptr;
};

// =============================================================================
// Pipeline
// =============================================================================

struct WGPipelineWrapper {
	enum Type { RENDER,
		COMPUTE };
	Type type = RENDER;
	union {
		WGPURenderPipeline render_handle;
		WGPUComputePipeline compute_handle;
	};
	WGShader *shader = nullptr;
	// Specialized shader modules created with pipeline-specific specialization constants.
	// If non-null, these are owned by this pipeline and must be released.
	WGPUShaderModule specialized_modules[6] = {};
	// WebGPU has no pipeline-level stencil reference — it must be set dynamically
	// via wgpuRenderPassEncoderSetStencilReference(). Stored here at creation time,
	// applied when the pipeline is bound.
	uint32_t stencil_reference = 0;
	// WebGPU bakes stripIndexFormat into the pipeline state, but Godot only knows
	// the index format at bind time. For strip topologies we create both Uint16 and
	// Uint32 variants and select the correct one at draw time.
	WGPURenderPipeline render_handle_u16 = nullptr;
	bool is_strip = false;
	// Blends with BLEND_FACTOR_(ONE_MINUS_)CONSTANT_ALPHA. WebGPU's only blend
	// constant factor uses the whole constant color, so while this pipeline is
	// bound the driver sets the constant to (a, a, a, a) instead.
	bool blend_constant_alpha = false;
	// A compute pipeline is compiled on its first bind, not when it is created:
	// see RenderingDeviceDriverWebGPU::compute_pipeline_create(). Until then
	// compute_handle is null and this holds what creating it needs; the shader
	// outlives the pipeline (RenderingDevice frees dependent pipelines first).
	bool compute_pending = false;
	LocalVector<RenderingDeviceDriver::PipelineSpecializationConstant> pending_specialization_constants;
	CharString pending_label;
	// Background prewarm (RenderingDeviceDriverWebGPU::_prewarm_compute_pipelines()):
	// queued, or the token of an asynchronous creation in flight (0 if none).
	bool queued_for_prewarm = false;
	uint64_t prewarm_token = 0;

	WGPipelineWrapper() : render_handle(nullptr) {}
};

// =============================================================================
// Uniform Set (Bind Group)
// =============================================================================

struct WGUniformSet {
	WGPUBindGroup handle = nullptr;
	uint32_t set_index = 0;
	LocalVector<WGPUTextureView> temp_views; // Re-dimensioned views for Cube↔2D fixups.
	// Shadow textures/views for read_write storage texture splits.
	// Owned by the uniform set; released when the set is destroyed.
	LocalVector<WGPUTexture> rw_shadow_textures;
	LocalVector<WGPUTextureView> rw_shadow_views;

	// Tracks which source→shadow registrations this uniform set created in
	// rw_shadow_copy_map, so they can be deregistered when the set is freed.
	// Also used to refresh the shadow (GPU-side copy from source) immediately
	// before each compute dispatch that reads it — see
	// RenderingDeviceDriverWebGPU::command_bind_compute_uniform_sets(). The
	// shadow is otherwise only ever refreshed by texture_update() (a CPU
	// upload), which never happens for a purely GPU-authored (compute/render-
	// written) source texture, leaving such a shadow permanently at its
	// initial (zeroed) contents. Task 9.5, "bokeh_dof.glsl solid-black" round.
	struct RWShadowRegistration {
		WGPUTexture source;
		WGPUTexture shadow;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 0;
	};
	LocalVector<RWShadowRegistration> rw_shadow_registrations;

	// Rebind cache: when a bind group is created with shader A's BGL but
	// needs to be used with shader B's pipeline (different BGL), we
	// re-create the bind group with the correct BGL and cache it.
	LocalVector<WGPUBindGroupEntry> cached_entries;
	WGShader *source_shader = nullptr;
	// The layout `handle` was created with, i.e. what Dawn will compare against
	// the pipeline layout if `handle` is bound as-is. Needed because every
	// _get_compatible_bind_group() early-out returns `handle`, and the caller
	// has to be able to record which layout it actually bound.
	WGPUBindGroupLayout handle_layout = nullptr;
	HashMap<WGPUBindGroupLayout, WGPUBindGroup> rebind_cache;
	// Small front for rebind_cache, searched linearly before the hash lookup that
	// was the driver's largest per-bind cost (TASKS.md 2.30). Four entries, not
	// one: a material's set is bound once per pass and each pass (shadow, depth
	// pre-pass, colour) binds it against a different variant's layout, so a single
	// most-recent slot never hit. Entries mirror rebind_cache (a null group is a
	// cached failure) and are cleared with it in _purge_rebind_caches_for_shader(),
	// so none can outlive its key.
	static constexpr uint32_t REBIND_RECENT_COUNT = 4;
	WGPUBindGroupLayout rebind_recent_layout[REBIND_RECENT_COUNT] = {};
	WGPUBindGroup rebind_recent_group[REBIND_RECENT_COUNT] = {};
	uint32_t rebind_recent_next = 0;

	void rebind_recent_store(WGPUBindGroupLayout p_layout, WGPUBindGroup p_group) {
		rebind_recent_layout[rebind_recent_next] = p_layout;
		rebind_recent_group[rebind_recent_next] = p_group;
		rebind_recent_next = (rebind_recent_next + 1) % REBIND_RECENT_COUNT;
	}
	void rebind_recent_forget(WGPUBindGroupLayout p_layout) {
		for (uint32_t i = 0; i < REBIND_RECENT_COUNT; i++) {
			if (rebind_recent_layout[i] == p_layout) {
				rebind_recent_layout[i] = nullptr;
				rebind_recent_group[i] = nullptr;
			}
		}
	}

	// Maps binding index → WGTexture* for texture entries, used during rebind
	// to create compatible views when the target BGL has a different dimension.
	HashMap<uint32_t, WGTexture *> bound_textures;

	// Dynamic buffers in binding order (Task 7.5). Populated by uniform_set_create
	// whenever a UNIFORM_TYPE_*_BUFFER_DYNAMIC binding resolves to a WGBuffer with
	// is_dynamic()==true. command_bind_*_uniform_sets walks this list in order to
	// unpack per-set dynamic offsets from the mask returned by
	// uniform_sets_get_dynamic_offsets and pass them to wgpuRenderPassEncoderSetBindGroup.
	LocalVector<WGBuffer *> dynamic_buffers;
};

// =============================================================================
// Swap Chain
// =============================================================================

struct WGSwapChain {
	WGPUSurface surface = nullptr;
	WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
	uint32_t width = 0, height = 0;
	WGRenderPass *render_pass = nullptr;
	uint64_t surface_id = 0; // Context driver SurfaceID — used to look up size on resize.
	WGPUTexture current_texture = nullptr; // Released after each frame.
	WGPUTextureView current_view = nullptr;
	WGFramebuffer *current_framebuffer = nullptr;
	bool configured = false;
};

// =============================================================================
// Command Queue / Pool / Buffer
// =============================================================================

struct WGCommandQueue {
	WGPUQueue queue = nullptr;
};

struct WGCommandBuffer; // Forward declaration for WGCommandPool.

struct WGCommandPool {
	RDD::CommandBufferType buffer_type = RDD::COMMAND_BUFFER_TYPE_PRIMARY;
	// Command buffers are pool-owned, as in the Vulkan driver: the base API has
	// no command_buffer_free(), so command_pool_free() is the only chance to
	// release them.
	LocalVector<WGCommandBuffer *> command_buffers_created;
};

struct WGQueryPool; // Forward declaration for WGCommandBuffer.

struct WGCommandBuffer {
	WGPUCommandEncoder encoder = nullptr;
	WGPUCommandBuffer finished_buffer = nullptr;

	// Current active encoder states.
	WGPURenderPassEncoder render_encoder = nullptr;
	WGPUComputePassEncoder compute_encoder = nullptr;

	enum ActiveEncoder { NONE,
		RENDER,
		COMPUTE };
	ActiveEncoder active_encoder = NONE;

	// Push constant emulation state.
	static constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 128;
	uint8_t push_constant_data[MAX_PUSH_CONSTANT_SIZE] = {};
	uint32_t push_constant_data_len = 0;
	bool push_constants_dirty = false;
	// Combined bind group for the PC group (includes both material resources and PC ring buffer).
	// Updated by command_bind_render_uniform_sets when the PC group is bound.
	WGPUBindGroup current_pc_bind_group = nullptr;
	// Last PC ring offset successfully flushed by _flush_push_constants.
	// Used by command_bind_render_uniform_sets to set the correct dynamic offset
	// when rebinding the merged PC group (avoids stale offset=0 placeholder).
	uint32_t last_flushed_pc_offset = 0;
	// Task 7.5: When the PC group also contains material dynamic-offset UBOs,
	// _flush_push_constants must preserve those offsets while patching in the new
	// PC ring offset. Stored in binding order; the PC ring's own slot is appended
	// at pc_group_material_dyn_count by _flush_push_constants each draw.
	static constexpr uint32_t MAX_PC_GROUP_MATERIAL_DYN = 7; // MAX_DYNAMIC_BUFFERS - 1 (PC ring takes 1 slot).
	uint32_t pc_group_material_dyn_offsets[MAX_PC_GROUP_MATERIAL_DYN] = {};
	uint32_t pc_group_material_dyn_count = 0;

	// Render state tracking.
	struct RenderState {
		WGRenderPass *render_pass = nullptr;
		WGFramebuffer *framebuffer = nullptr;
		uint32_t current_subpass = 0;
		WGPipelineWrapper *current_pipeline = nullptr;
		WGPUIndexFormat current_index_format = WGPUIndexFormat_Uint32;
		// The blend constants Godot last set in this pass, and whether they were
		// applied as (a, a, a, a) for a constant-alpha pipeline.
		WGPUColor blend_constants = {};
		bool blend_constants_set = false;
		bool blend_constants_as_alpha = false;
		// Far edge (x + width, y + height) of the render area in attachment coordinates.
		uint32_t render_area_width = 0;
		uint32_t render_area_height = 0;

		// Redundant state elimination for per-draw calls.
		WGPUBuffer current_index_buffer = nullptr;
		uint64_t current_index_offset = 0;
		static constexpr uint32_t MAX_VERTEX_BINDINGS = 8;
		WGPUBuffer current_vertex_buffers[MAX_VERTEX_BINDINGS] = {};
		uint64_t current_vertex_offsets[MAX_VERTEX_BINDINGS] = {};
		// Textures used as render attachments in the CURRENT pass only.
		// Used to detect intra-pass sync scope violations when a texture
		// has both TextureBinding and RenderAttachment usage in the same pass.
		// Note: cross-pass synchronization is guaranteed by the WebGPU spec
		// (passes within a single command encoder execute sequentially with
		// implicit barriers at usage scope boundaries).
		static constexpr uint32_t MAX_ATTACHMENT_TEXTURES = 64;
		WGPUTexture current_pass_attachments[MAX_ATTACHMENT_TEXTURES] = {};
		uint32_t current_pass_attachment_count = 0;
		void reset_current_pass_attachments() { current_pass_attachment_count = 0; }
		void add_current_pass_attachment(WGPUTexture t) {
			if (!t) {
				return;
			}
			for (uint32_t i = 0; i < current_pass_attachment_count; i++) {
				if (current_pass_attachments[i] == t) {
					return;
				}
			}
			if (current_pass_attachment_count < MAX_ATTACHMENT_TEXTURES) {
				current_pass_attachments[current_pass_attachment_count++] = t;
			}
		}

		// Textures bound (in any capacity) so far in the CURRENT compute pass,
		// restricted to textures capable of both storage and sampled usage.
		// Detects the compute analog of the render-attachment conflict above:
		// a single Godot "compute list" (compute_list_begin/end) can span
		// multiple dispatches sharing one WGPUComputePassEncoder the whole
		// time (command_pipeline_barrier is a no-op between them, see
		// CLAUDE.md's "Barriers" note), so a texture written as a storage
		// image in one dispatch and then sampled in a later dispatch of the
		// SAME compute list -- e.g. FSR1/FSR2's EASU-write-then-RCAS-sample
		// ping-pong on their shared upscale texture -- lands both usages in
		// one synchronization scope, which Dawn rejects ("includes writable
		// usage and another usage in the same synchronization scope"),
		// invalidating the whole command buffer. See webgpu_notes/TASKS.md's
		// FSR1/FSR2 tasks.
		static constexpr uint32_t MAX_COMPUTE_PASS_TEXTURES = 32;
		WGPUTexture current_compute_pass_textures[MAX_COMPUTE_PASS_TEXTURES] = {};
		uint32_t current_compute_pass_texture_count = 0;
		void reset_current_compute_pass_textures() { current_compute_pass_texture_count = 0; }
		bool has_current_compute_pass_texture(WGPUTexture t) const {
			for (uint32_t i = 0; i < current_compute_pass_texture_count; i++) {
				if (current_compute_pass_textures[i] == t) {
					return true;
				}
			}
			return false;
		}
		void add_current_compute_pass_texture(WGPUTexture t) {
			if (!t || has_current_compute_pass_texture(t)) {
				return;
			}
			if (current_compute_pass_texture_count < MAX_COMPUTE_PASS_TEXTURES) {
				current_compute_pass_textures[current_compute_pass_texture_count++] = t;
			}
		}
	} render_state;

	// Bind group state tracking for redundancy elimination.
	// Tracks what's currently bound at each bind group slot to skip redundant SetBindGroup calls.
	static constexpr uint32_t MAX_BIND_GROUPS = 4;
	WGPUBindGroup bound_bind_groups[MAX_BIND_GROUPS] = {};
	WGShader *bound_shader = nullptr; // Shader whose BGL the bound groups were created for.

	// Full bind group state for mid-pass restart (push constant ring overflow).
	// Stores the resolved bind group + dynamic offsets for each slot so we can
	// rebind everything after ending/restarting the render pass.
	static constexpr uint32_t MAX_BIND_GROUP_DYN_OFFSETS = 9; // 8 material + 1 PC ring
	struct BoundGroupState {
		WGPUBindGroup group = nullptr;
		uint32_t dynamic_offsets[MAX_BIND_GROUP_DYN_OFFSETS] = {};
		uint32_t dynamic_offset_count = 0;
		// The layout `group` was built against, and the uniform set it came
		// from (null for the empty/gap group and for the PC-only group, which
		// have no uniform set behind them). Together these let the draw path
		// check each slot against the pipeline layout and re-adapt from
		// `source_set` when they disagree -- see
		// _reconcile_render_bind_groups(). `source_set` is only dereferenced
		// during the same frame it was bound in; RenderingDevice defers
		// uniform-set deletion to a frame boundary, which is the same
		// assumption the restart path above already makes about `group`.
		WGPUBindGroupLayout layout = nullptr;
		WGUniformSet *source_set = nullptr;
	};
	BoundGroupState last_bound_state[MAX_BIND_GROUPS] = {};

	void invalidate_bind_groups() {
		for (uint32_t i = 0; i < MAX_BIND_GROUPS; i++) {
			bound_bind_groups[i] = nullptr;
		}
		bound_shader = nullptr;
		current_pc_bind_group = nullptr;
		last_flushed_pc_offset = 0;
		pc_group_material_dyn_count = 0;
	}

	// Record that the shared empty bind group is now set at a pipeline-layout
	// gap slot, so the draw-time reconcile and the mid-pass restart both see
	// the slot as legitimately occupied rather than stale or empty.
	void note_gap_group(uint32_t p_idx, WGPUBindGroup p_empty_group, WGPUBindGroupLayout p_empty_layout) {
		if (p_idx >= MAX_BIND_GROUPS) {
			return;
		}
		BoundGroupState &bs = last_bound_state[p_idx];
		bs.group = p_empty_group;
		bs.layout = p_empty_layout;
		bs.source_set = nullptr;
		bs.dynamic_offset_count = 0;
	}

	// last_bound_state is deliberately NOT cleared by invalidate_bind_groups():
	// that runs on a shader change *within* a pass, where the groups are still
	// bound on the encoder and the mid-pass restart path needs them. Clear it
	// only where a fresh encoder makes the slots genuinely empty -- a new
	// render/compute pass or subpass.
	void reset_bound_group_state() {
		for (uint32_t i = 0; i < MAX_BIND_GROUPS; i++) {
			last_bound_state[i] = BoundGroupState();
		}
	}

	// Track query pools that had timestamps written, for resolve at command buffer end.
	LocalVector<WGQueryPool *> written_query_pools;

	// Helpers.
	void end_active_encoder() {
		if (render_encoder) {
			wgpuRenderPassEncoderEnd(render_encoder);
			wgpuRenderPassEncoderRelease(render_encoder);
			render_encoder = nullptr;
		}
		if (compute_encoder) {
			wgpuComputePassEncoderEnd(compute_encoder);
			wgpuComputePassEncoderRelease(compute_encoder);
			compute_encoder = nullptr;
		}
		active_encoder = NONE;
	}
};

// =============================================================================
// Fence
// =============================================================================

struct WGFence {
	bool signaled = false;
	uint64_t submission_id = 0;
	bool work_done_pending = false; // True while wgpuQueueOnSubmittedWorkDone callback is in flight.
	bool freed = false; // Freed while callback pending; callback will delete.
};

// =============================================================================
// Semaphore (mostly no-op on WebGPU — single queue)
// =============================================================================

struct WGSemaphore {};

// =============================================================================
// Query Pool
// =============================================================================

struct WGQueryPool {
	WGPUQuerySet handle = nullptr;
	WGPUBuffer resolve_buffer = nullptr; // GPU buffer for query set resolve (CopySrc | QueryResolve).
	WGPUBuffer readback_buffer = nullptr; // CPU-readable staging buffer (CopyDst | MapRead).
	uint32_t count = 0;
	bool is_real = false; // True if backed by actual timestamp-query hardware.

	// Shadow CPU buffer for async readback results.
	uint64_t *cpu_results = nullptr;
	bool readback_pending = false;
	uint32_t map_generation = 0; // Incremented each time mapAsync is issued; stale callbacks are ignored.
	bool freed = false; // Freed while readback pending; callback will clean up.
};

#endif // WEBGPU_ENABLED
