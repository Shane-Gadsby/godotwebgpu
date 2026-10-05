/**************************************************************************/
/*  rendering_shader_container_webgpu.h                                   */
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

#if defined(WEBGPU_ENABLED) || defined(WEBGPU_SHADER_BAKER_ENABLED)

#include "core/string/ustring.h"
#include "drivers/webgpu/spirv_lite_reflect.h"
#include "servers/rendering/rendering_shader_container.h"

class RenderingShaderContainerWebGPU : public RenderingShaderContainer {
	GDSOFTCLASS(RenderingShaderContainerWebGPU, RenderingShaderContainer);

public:
	// Format identifier for WebGPU shader containers.
	static constexpr uint32_t FORMAT_WEBGPU = 0x57475055; // "WGPU"
	// 2 added the baked image-declaration footer block and SPIR-V omission, both
	// of which are gated on HeaderData::flags bits rather than on this number: a
	// version 1 container has flags == 0 (the field existed and was never
	// written), so it still reads correctly as "nothing baked, SPIR-V present".
	static constexpr uint32_t FORMAT_VERSION = 2;
	static constexpr uint32_t NO_PUSH_CONSTANTS = UINT32_MAX;

	enum HeaderFlags : uint32_t {
		// The footer carries one baked image-declaration list per stage, plus
		// each stage's "declares specialization constants" bit. The runtime
		// reads those instead of walking the SPIR-V for them.
		FLAG_IMAGE_DECLS_BAKED = 1 << 0,
		// The per-shader SPIR-V payload is absent -- `code_compressed_bytes` is
		// empty for every stage. Only set when the bake established that nothing
		// at runtime can need it: every stage converted to WGSL successfully, so
		// there is no Tint fallback to run, and no stage needs the legacy
		// specialize-by-re-patching-SPIR-V path. See _set_code_from_spirv().
		FLAG_SPIRV_OMITTED = 1 << 1,
	};

	struct HeaderData {
		uint32_t push_constant_bind_group = NO_PUSH_CONSTANTS; // UINT32_MAX = no push constants.
		uint32_t push_constant_binding = 0;
		uint32_t flags = 0;
	};

	// One baked WGSL stage, as an (offset, length) window into `source_bytes`
	// rather than a copy of the text. See `wgsl_view` below.
	struct WGSLView {
		uint32_t offset = 0;
		uint32_t length = 0; // 0 = this stage was not baked.
	};

protected:
	HeaderData header_data;

	// Precompiled WGSL per shader stage, parallel to `shaders`, in one of two
	// representations -- at most one of which is populated for a given stage:
	//
	// `wgsl_code` is owned text, produced in memory by _set_code_from_spirv():
	// - Export-time bake (native editor, WEBGPU_SHADER_BAKER_ENABLED): always,
	//   so exported .pck's ship with WGSL and the runtime driver skips Tint.
	// - Runtime (web build, WEBGPU_ENABLED): only reached if a shader is ever
	//   compiled from source in the browser itself (not the normal path for
	//   exported projects, which consume pre-baked containers).
	//
	// `wgsl_view` is a window into `source_bytes` -- the buffer from_bytes()
	// parsed -- and is what the runtime actually uses, because loading a
	// container is the hot path: an exported project's containers carry ~60 MB
	// of baked WGSL between them, and copying each stage's text out into its own
	// CharString was a full pass over all of it (plus one allocation per stage)
	// for every load. The driver has to copy the text anyway, into a buffer it
	// can mutate, so the intermediate copy bought nothing. A stage that was
	// baked in memory has an entry in `wgsl_code`; a stage that came off disk
	// has one in `wgsl_view`; get_wgsl_code() reads whichever is there.
	//
	// An empty entry in both means "not baked" (e.g. a stage Tint could not
	// convert at export time), and the driver falls back to translating that
	// stage's SPIR-V itself.
	Vector<CharString> wgsl_code;
	Vector<WGSLView> wgsl_view;

	// Per stage, the image declarations spirv_lite_reflect::extract_raw_image_decls()
	// found in that stage's raw SPIR-V, and whether that stage declares any
	// specialization constants.
	//
	// Both are things the runtime driver used to re-derive from the SPIR-V on
	// every load, and they are the only things it needed the SPIR-V *for* once
	// the WGSL is baked. Recording them here is what makes FLAG_SPIRV_OMITTED
	// possible -- and SPIR-V is the larger of a container's two payloads, so
	// dropping it is the larger saving. Populated by the bake, or read back from
	// the footer; empty on a container that predates FLAG_IMAGE_DECLS_BAKED, in
	// which case the driver falls back to walking the SPIR-V exactly as before.
	Vector<Vector<spirv_lite_reflect::RawImageDecl>> image_decls;
	Vector<bool> stage_has_spec_constants_flags;

	// The buffer from_bytes() was handed, retained so `wgsl_view`'s windows stay
	// valid for as long as this container does. PackedByteArray is refcounted, so
	// this is a refcount bump and not a copy -- see _from_bytes_begin()'s doc
	// comment on the base class.
	PackedByteArray source_bytes;

	// --- RenderingShaderContainer overrides ---

	virtual uint32_t _format() const override { return FORMAT_WEBGPU; }
	virtual uint32_t _format_version() const override { return FORMAT_VERSION; }

	/// Called by set_code_from_spirv() — stores raw SPIR-V bytes per stage
	/// (Dawn's WebGPU implementation supports WGPUShaderSourceSPIRV natively,
	/// so SPIR-V remains a valid runtime fallback), and additionally bakes
	/// WGSL per stage via the shared Tint pipeline (see spirv_to_wgsl.h).
	virtual bool _set_code_from_spirv(const ReflectShader &p_shader) override;

	// Serialization overrides for extra header data.
	virtual uint32_t _from_bytes_header_extra_data(const uint8_t *p_bytes) override;
	virtual uint32_t _to_bytes_header_extra_data(uint8_t *p_bytes) const override;

	// Serialization overrides for the baked-WGSL footer block.
	virtual void _from_bytes_begin(const PackedByteArray &p_bytes) override;
	virtual uint32_t _from_bytes_footer_extra_data(const uint8_t *p_bytes) override;
	virtual uint32_t _to_bytes_footer_extra_data(uint8_t *p_bytes) const override;

	// Serializes the baked image-declaration / specialization-constant block
	// that follows the WGSL in the footer. nullptr to size it, same convention
	// as the _to_bytes_* hooks.
	uint32_t _baked_metadata_to_bytes(uint8_t *p_bytes) const;

	// uint32 words per serialized RawImageDecl: set, binding, format, dim,
	// arrayed, signedness.
	static constexpr uint32_t IMAGE_DECL_WORDS = 6;

public:
	uint32_t get_push_constant_bind_group() const { return header_data.push_constant_bind_group; }
	uint32_t get_push_constant_binding() const { return header_data.push_constant_binding; }
	bool has_push_constants() const { return header_data.push_constant_bind_group != NO_PUSH_CONSTANTS; }

	// Returns the baked WGSL for a shader stage index (parallel to `shaders`) as
	// pointer + length, or false if this stage wasn't baked (caller should fall
	// back to translating its SPIR-V with Tint).
	//
	// The text is NOT NUL-terminated: when it comes from the footer it is a
	// window into the container bytes, where the next stage's length prefix
	// follows it immediately. Callers copy it into their own buffer anyway (the
	// driver's WGSL text passes mutate it in place), so they terminate it there.
	// The pointer is valid for as long as this container is.
	bool get_wgsl_code(uint32_t p_shader_index, const char *&r_wgsl, uint32_t &r_length) const;

	// Payload accounting for the whole process, published by the driver as part
	// of `godotWebGPUShaderStats` (see rendering_device_driver_webgpu.cpp).
	//
	// Every container carries two descriptions of the same shader -- the SPIR-V
	// it was compiled to and the WGSL baked from that SPIR-V -- and the split
	// between them is what decides whether shipping both is worth its cost. It
	// cannot be read off the .pck, because that mixes shaders in with every
	// other resource, and it cannot be inferred from the shader count, because
	// stage sizes vary by two orders of magnitude. So it is counted here, where
	// both numbers are exact, rather than estimated.
	struct LoadStats {
		uint64_t containers = 0; // Containers parsed by from_bytes().
		uint64_t spirv_bytes = 0; // SPIR-V across every stage of them.
		uint64_t wgsl_bytes = 0; // Baked WGSL across every stage of them.
		uint64_t stages_baked = 0;
		uint64_t stages_unbaked = 0; // Stages the runtime must run Tint on.
		double footer_parse_ms = 0.0; // Time in _from_bytes_footer_extra_data().
	};
	static const LoadStats &get_load_stats();

	// True when the footer carried baked image declarations (and per-stage
	// specialization-constant bits), so the runtime can skip its own SPIR-V walk.
	bool has_baked_image_decls() const { return (header_data.flags & FLAG_IMAGE_DECLS_BAKED) != 0; }

	// True when this container ships no SPIR-V at all. See FLAG_SPIRV_OMITTED.
	bool is_spirv_omitted() const { return (header_data.flags & FLAG_SPIRV_OMITTED) != 0; }

	// The baked image declarations for a stage, or nullptr if this container has
	// none (pre-FLAG_IMAGE_DECLS_BAKED, or an out-of-range index).
	const Vector<spirv_lite_reflect::RawImageDecl> *get_baked_image_decls(uint32_t p_shader_index) const;

	// Whether a stage declares specialization constants, as recorded at bake
	// time. Only meaningful when has_baked_image_decls() is true -- which is
	// also the only case where the SPIR-V may be gone and the runtime cannot
	// answer this itself.
	bool get_stage_has_spec_constants(uint32_t p_shader_index) const;

private:
	// One per process, not per container: the question these answer ("how many
	// bytes of shader does a load actually move?") is about the whole load.
	static LoadStats &_load_stats();

public:
	RenderingShaderContainerWebGPU();
	virtual ~RenderingShaderContainerWebGPU();
};

// =============================================================================
// Format Factory
// =============================================================================

class RenderingShaderContainerFormatWebGPU : public RenderingShaderContainerFormat {
	GDSOFTCLASS(RenderingShaderContainerFormatWebGPU, RenderingShaderContainerFormat);

public:
	virtual Ref<RenderingShaderContainer> create_container() const override {
		return Ref<RenderingShaderContainerWebGPU>(memnew(RenderingShaderContainerWebGPU));
	}

	virtual ShaderLanguageVersion get_shader_language_version() const override {
		// Vulkan-flavour GLSL 1.1 — same as the Vulkan driver.
		return SHADER_LANGUAGE_VULKAN_VERSION_1_1;
	}

	virtual ShaderSpirvVersion get_shader_spirv_version() const override {
		// SPIR-V 1.3 — required so glslang emits SSBOs as StorageClass::StorageBuffer
		// (not the old-style StorageClass::Uniform + BufferBlock used in SPIR-V 1.0).
		// Tint correctly converts StorageClass::StorageBuffer → var<storage, read/read_write>.
		// SPIR-V 1.3 requires Vulkan 1.1 client, which matches our language version.
		return SHADER_SPIRV_VERSION_1_3;
	}
};

#endif // WEBGPU_ENABLED
