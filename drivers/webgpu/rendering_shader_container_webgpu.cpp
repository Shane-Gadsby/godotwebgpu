/**************************************************************************/
/*  rendering_shader_container_webgpu.cpp                                 */
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

#if defined(WEBGPU_ENABLED) || defined(WEBGPU_SHADER_BAKER_ENABLED)

#include "rendering_shader_container_webgpu.h"

#ifdef WEBGPU_SHADER_BAKER_ENABLED
#include "drivers/webgpu/wgsl_bake_subprocess.h"
#endif

#include "core/os/os.h"
#include "drivers/webgpu/spirv_lite_reflect.h"

#include <cstring>

// =========================================================================
// SPIR-V Storage + WGSL Precompilation
// =========================================================================
//
// Architecture decision (see copilot-instructions.md #6):
//   GLSL → SPIR-V (glslang) → stored directly as WGPUShaderSourceSPIRV.
//   Dawn's emdawnwebgpu port natively supports WGPUShaderSourceSPIRV, so
//   SPIR-V is kept as a runtime fallback even once WGSL is baked below.
//
// WGSL is additionally precompiled here (native editor only,
// WEBGPU_SHADER_BAKER_ENABLED -- see
// editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.cpp)
// by shelling out to bin/tint_convert_cli per stage (wgsl_bake_subprocess.h),
// NOT by calling Tint in-process. Tint can abort() with an internal-compiler-
// error on a shader variant it doesn't handle, and export-time baking
// compiles every declared variant (not just ones a game actually exercises
// at runtime) -- confirmed in practice: baking this way took down the whole
// editor process on a real project's shader. Running Tint in a subprocess
// means that abort() only kills the subprocess; a failed stage is simply
// left unbaked here (get_wgsl_code() returns nullptr for it) and the
// runtime driver's own Tint fallback (which already handles that case)
// takes over in the browser exactly as if this file had never baked it.
//
// A plain WEBGPU_ENABLED build (the web runtime itself) never reaches this
// function's WEBGPU_SHADER_BAKER_ENABLED branch and never bakes WGSL here --
// only SPIR-V storage happens, unchanged from before baking existed. The
// runtime path (shader_create_from_container() in
// rendering_device_driver_webgpu.cpp) has its own independent, already-
// isolated-by-being-in-the-browser Tint fallback for that case.
//
// Push constant handling:
//   Godot's push constants are emulated via a uniform buffer at a fixed
//   bind group slot (default: group 3, binding 0).
//   The bind group slot is recorded in HeaderData and used by the driver
//   to create the pipeline layout and bind the ring buffer at draw time.

#ifdef WEBGPU_SHADER_BAKER_ENABLED
// True when this WGSL text declares at least one `@id(N)` override, i.e. Tint
// kept the module's specialization constants as something a pipeline can set
// through WebGPU's own pipeline constants.
//
// Must stay the same test the runtime driver makes on the same text --
// shader_create_from_container()'s `@id(` scan in
// rendering_device_driver_webgpu.cpp, which is the authority, since what it
// decides is what actually happens. The two agree because the test is
// structural (`@id(`, digits, `)`) and because none of the driver's WGSL text
// passes that run before its scan touch an `@id(...)` declaration. They do
// rewrite texture declarations, textureLoad call sites and storage-texture
// splits, so this is worth stating rather than assuming.
//
// Being *stricter* than the driver is harmless here (the container keeps its
// SPIR-V unnecessarily); being looser would drop bytes the driver then needs.
static bool _wgsl_declares_override(const CharString &p_wgsl) {
	const char *p = p_wgsl.get_data();
	if (p == nullptr) {
		return false;
	}
	while ((p = strstr(p, "@id(")) != nullptr) {
		p += 4;
		bool has_digits = false;
		while (*p >= '0' && *p <= '9') {
			p++;
			has_digits = true;
		}
		if (has_digits && *p == ')') {
			return true;
		}
	}
	return false;
}

// WEBGPU_BAKE_KEEP_SPIRV=1 makes the bake ship the SPIR-V even where nothing at
// runtime can need it.
//
// Two uses. It is the A/B for the saving itself -- two exports from one build,
// with and without -- which is how the numbers in webgpu_notes/TASKS.md Task 14
// were measured, and how they can be re-measured on another project without
// building the engine twice. And it is an escape hatch: this is the one change
// in this file that *discards* data, so there is a way to put it back without
// rebuilding, if a shader ever turns out to need bytes the rule thought it
// could not.
//
// Read once: baking runs on many WorkerThreadPool threads, and a
// function-local static is initialized exactly once under the C++11 rules.
static bool _bake_keeps_spirv() {
	static const bool keep = OS::get_singleton()->get_environment("WEBGPU_BAKE_KEEP_SPIRV") == "1";
	return keep;
}
#endif

bool RenderingShaderContainerWebGPU::_set_code_from_spirv(const ReflectShader &p_shader) {
	const uint32_t stage_count = p_shader.shader_stages.size();
	shaders.resize(stage_count);
	wgsl_code.resize(stage_count);
	image_decls.resize(stage_count);
	stage_has_spec_constants_flags.resize_initialized(stage_count);
	// Nothing to view: this path bakes the text in memory, so every stage's WGSL
	// lives in wgsl_code. wgsl_view is only populated by the footer parse.
	wgsl_view.clear();

	for (uint32_t i = 0; i < stage_count; i++) {
		const ReflectShaderStage &stage = p_shader.shader_stages[i];
		// Store raw SPIR-V bytes directly (runtime fallback if WGSL bake fails).
		Vector<uint8_t> spirv_bytes = stage.spirv_data();
		shaders.write[i].shader_stage = stage.shader_stage;
		shaders.write[i].code_compression_flags = 0; // No compression.
		shaders.write[i].code_decompressed_size = 0; // 0 = not compressed (use raw bytes).
		shaders.write[i].code_compressed_bytes = spirv_bytes;

#ifdef WEBGPU_SHADER_BAKER_ENABLED
		// Precompile to WGSL via a tint_convert_cli subprocess. On failure,
		// leave wgsl_code[i] empty — the runtime driver falls back to
		// translating from SPIR-V itself.
		String wgsl = webgpu::bake_wgsl_via_subprocess(spirv_bytes.ptr(), (int)spirv_bytes.size());
		wgsl_code.write[i] = wgsl.is_empty() ? CharString() : wgsl.utf8();

		// Everything the runtime driver reads this stage's raw SPIR-V for, read
		// here instead so it does not have to. Both are pure functions of the
		// bytes, so doing it at export time is doing it once rather than on
		// every load of every shader in every session.
		image_decls.write[i] = spirv_lite_reflect::extract_raw_image_decls(spirv_bytes);
		stage_has_spec_constants_flags.write[i] = spirv_lite_reflect::has_spec_id_decoration(spirv_bytes);

#else
		wgsl_code.write[i] = CharString();
#endif
	}

#ifdef WEBGPU_SHADER_BAKER_ENABLED
	header_data.flags |= FLAG_IMAGE_DECLS_BAKED;

	// Decide whether this container has to ship its SPIR-V at all.
	//
	// The runtime driver reads a stage's SPIR-V for exactly four things, and
	// three of them are now covered: the pre-DCE image declarations and the
	// "declares specialization constants" bit are baked above, and the Tint
	// fallback is moot for a stage whose WGSL is already here. The fourth is the
	// legacy specialization path -- re-patching the SPIR-V with constant values
	// and re-converting it -- which the driver takes for a shader that declares
	// specialization constants but whose WGSL carries no `@id(N) override` to
	// set them through. That one genuinely needs the bytes, so a stage in that
	// shape keeps them for the whole container.
	//
	// Erring towards keeping the SPIR-V is the safe direction: it costs size,
	// where erring the other way would mean a shader silently rendering with
	// default specialization values. The driver says so out loud if this
	// decision ever turns out wrong -- see shader_create_from_container().
	bool spirv_needed = false;
	for (uint32_t i = 0; i < stage_count; i++) {
		if (wgsl_code[i].is_empty()) {
			spirv_needed = true; // No WGSL: the runtime must run Tint on the bytes.
			break;
		}
		if (stage_has_spec_constants_flags[i] && !_wgsl_declares_override(wgsl_code[i])) {
			spirv_needed = true; // Legacy specialization path.
			break;
		}
	}

	if (!spirv_needed && !_bake_keeps_spirv()) {
		header_data.flags |= FLAG_SPIRV_OMITTED;
		for (uint32_t i = 0; i < stage_count; i++) {
			shaders.write[i].code_compressed_bytes = Vector<uint8_t>();
		}
	}
#endif

	// Decide push constant bind group slot.
	if (p_shader.push_constant_size > 0) {
		// Convention: push constants use bind group 3, binding PUSH_CONSTANT_RING_BINDING (120).
		// Chosen high enough to avoid collision with split combined-sampler bindings
		// (original binding N → sampler@N*2, image@N*2+1; max reasonable N ~20 → max~41).
		// Must match the binding in spirv_preprocess::convert_push_constants_to_uniforms()
		// and PUSH_CONSTANT_RING_BINDING in rendering_device_driver_webgpu.h.
		header_data.push_constant_bind_group = 3;
		header_data.push_constant_binding = 120; // PUSH_CONSTANT_RING_BINDING
	} else {
		header_data.push_constant_bind_group = RenderingShaderContainerWebGPU::NO_PUSH_CONSTANTS;
		header_data.push_constant_binding = 120; // PUSH_CONSTANT_RING_BINDING
	}

	return true;
}

// =========================================================================
// Serialization
// =========================================================================

uint32_t RenderingShaderContainerWebGPU::_from_bytes_header_extra_data(const uint8_t *p_bytes) {
	if (p_bytes) {
		memcpy(&header_data, p_bytes, sizeof(HeaderData));
	}
	return sizeof(HeaderData);
}

uint32_t RenderingShaderContainerWebGPU::_to_bytes_header_extra_data(uint8_t *p_bytes) const {
	if (p_bytes) {
		memcpy(p_bytes, &header_data, sizeof(HeaderData));
	}
	return sizeof(HeaderData);
}

// Footer block: one [uint32_t length][length bytes, no NUL] entry per shader
// stage (parallel to `shaders`), length 0 meaning "not baked". Written/read
// after all per-shader data, so `shaders.size()` is already authoritative on
// both the writing side (set by _set_code_from_spirv()) and the reading side
// (set from the container header before _from_bytes_footer_extra_data() runs).
uint32_t RenderingShaderContainerWebGPU::_to_bytes_footer_extra_data(uint8_t *p_bytes) const {
	uint32_t offset = 0;
	for (int i = 0; i < shaders.size(); i++) {
		// Either representation can be the live one here: a container built by
		// the baker has owned text in wgsl_code, one that was read back from a
		// file has views into source_bytes. Writing out a container that was
		// read in is not something the engine does today, but a format that can
		// only round-trip one of its two representations is a trap, so both are
		// handled.
		const char *wgsl = nullptr;
		uint32_t len = 0;
		get_wgsl_code((uint32_t)i, wgsl, len);
		if (p_bytes) {
			memcpy(p_bytes + offset, &len, sizeof(uint32_t));
		}
		offset += sizeof(uint32_t);
		if (len > 0) {
			if (p_bytes) {
				memcpy(p_bytes + offset, wgsl, len);
			}
			offset += len;
		}
	}

	if (header_data.flags & FLAG_IMAGE_DECLS_BAKED) {
		offset += _baked_metadata_to_bytes(p_bytes ? p_bytes + offset : nullptr);
	}

	return offset;
}

// Second footer block, written only when FLAG_IMAGE_DECLS_BAKED is set. Per
// stage: a flags word (bit 0 = this stage declares specialization constants), a
// declaration count, and that many fixed-width records. Fixed-width because
// every field is already a SPIR-V operand, so there is nothing to compress and
// nothing variable-length to delimit.
uint32_t RenderingShaderContainerWebGPU::_baked_metadata_to_bytes(uint8_t *p_bytes) const {
	uint32_t offset = 0;
	const Vector<spirv_lite_reflect::RawImageDecl> empty;
	for (int i = 0; i < shaders.size(); i++) {
		const uint32_t stage_flags = (i < stage_has_spec_constants_flags.size() && stage_has_spec_constants_flags[i]) ? 1u : 0u;
		const Vector<spirv_lite_reflect::RawImageDecl> &decls = (i < image_decls.size()) ? image_decls[i] : empty;
		const uint32_t count = (uint32_t)decls.size();

		if (p_bytes) {
			memcpy(p_bytes + offset, &stage_flags, sizeof(uint32_t));
		}
		offset += sizeof(uint32_t);
		if (p_bytes) {
			memcpy(p_bytes + offset, &count, sizeof(uint32_t));
		}
		offset += sizeof(uint32_t);

		for (uint32_t j = 0; j < count; j++) {
			const spirv_lite_reflect::RawImageDecl &d = decls[j];
			const uint32_t words[IMAGE_DECL_WORDS] = {
				d.set,
				d.binding,
				d.spv_format,
				d.spv_dim,
				d.arrayed,
				(uint32_t)d.int_signedness,
			};
			if (p_bytes) {
				memcpy(p_bytes + offset, words, sizeof(words));
			}
			offset += (uint32_t)sizeof(words);
		}
	}
	return offset;
}

void RenderingShaderContainerWebGPU::_from_bytes_begin(const PackedByteArray &p_bytes) {
	// Retained so the windows _from_bytes_footer_extra_data() records below stay
	// valid for this container's lifetime. Refcounted, so this copies nothing.
	source_bytes = p_bytes;
}

uint32_t RenderingShaderContainerWebGPU::_from_bytes_footer_extra_data(const uint8_t *p_bytes) {
	const uint64_t parse_from = OS::get_singleton()->get_ticks_usec();

	uint32_t offset = 0;
	wgsl_code.clear();
	wgsl_view.resize(shaders.size());

	// Offsets are recorded relative to the start of the retained buffer, so a
	// view can be resolved later without holding on to p_bytes itself. If
	// _from_bytes_begin() did not see the buffer (no path does that today, but a
	// future caller bypassing from_bytes() would), fall back to copying the text
	// so the container is still correct rather than silently empty.
	const uint8_t *base = source_bytes.ptr();
	const bool can_view = base != nullptr && p_bytes >= base && p_bytes <= base + source_bytes.size();
	const uint32_t footer_start = can_view ? (uint32_t)(p_bytes - base) : 0;
	if (!can_view) {
		wgsl_code.resize(shaders.size());
	}

	// How many bytes of footer there are to read, so a length or a count that
	// came off disk cannot walk off the end of the buffer. The base class checks
	// afterwards that the whole container was consumed exactly, which catches a
	// truncated footer -- but only *after* this function has already read, and a
	// declaration count is a 32-bit number from the file multiplied by a record
	// size. Stopping short makes that check fail, which is the right outcome for
	// a corrupt container; reading past the end is not.
	//
	// Zero when the buffer was not seen, which disables the guard rather than
	// rejecting everything: that path copies and is the pre-existing behavior.
	const uint64_t footer_available = can_view ? (uint64_t)source_bytes.size() - footer_start : UINT64_MAX;
	const auto has_room = [&](uint64_t p_at, uint64_t p_bytes_needed) {
		return p_at + p_bytes_needed <= footer_available;
	};

	uint64_t spirv_bytes = 0;
	uint64_t wgsl_bytes = 0;
	uint64_t baked = 0;
	uint64_t unbaked = 0;

	for (int i = 0; i < shaders.size(); i++) {
		spirv_bytes += (uint64_t)shaders[i].code_compressed_bytes.size();

		if (!has_room(offset, sizeof(uint32_t))) {
			break;
		}
		uint32_t len = 0;
		memcpy(&len, p_bytes + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		if (!has_room(offset, len)) {
			break;
		}

		if (len > 0) {
			if (can_view) {
				wgsl_view.write[i].offset = footer_start + offset;
				wgsl_view.write[i].length = len;
			} else {
				CharString cs;
				cs.resize_uninitialized(len + 1);
				memcpy(cs.ptrw(), p_bytes + offset, len);
				cs.ptrw()[len] = '\0';
				wgsl_code.write[i] = cs;
			}
			offset += len;
			wgsl_bytes += len;
			baked++;
		} else {
			unbaked++;
		}
	}

	image_decls.clear();
	stage_has_spec_constants_flags.clear();
	if (header_data.flags & FLAG_IMAGE_DECLS_BAKED) {
		image_decls.resize(shaders.size());
		stage_has_spec_constants_flags.resize_initialized(shaders.size());
		for (int i = 0; i < shaders.size(); i++) {
			if (!has_room(offset, 2 * sizeof(uint32_t))) {
				break;
			}
			uint32_t stage_flags = 0;
			memcpy(&stage_flags, p_bytes + offset, sizeof(uint32_t));
			offset += sizeof(uint32_t);
			uint32_t count = 0;
			memcpy(&count, p_bytes + offset, sizeof(uint32_t));
			offset += sizeof(uint32_t);

			stage_has_spec_constants_flags.write[i] = (stage_flags & 1u) != 0;

			if (!has_room(offset, (uint64_t)count * IMAGE_DECL_WORDS * sizeof(uint32_t))) {
				break;
			}

			Vector<spirv_lite_reflect::RawImageDecl> decls;
			decls.resize((int64_t)count);
			for (uint32_t j = 0; j < count; j++) {
				uint32_t words[IMAGE_DECL_WORDS];
				memcpy(words, p_bytes + offset, sizeof(words));
				offset += (uint32_t)sizeof(words);
				spirv_lite_reflect::RawImageDecl &d = decls.write[j];
				d.set = words[0];
				d.binding = words[1];
				d.spv_format = words[2];
				d.spv_dim = words[3];
				d.arrayed = words[4];
				d.int_signedness = (int32_t)words[5];
			}
			image_decls.write[i] = decls;
		}
	}

	LoadStats &stats = _load_stats();
	stats.containers++;
	stats.spirv_bytes += spirv_bytes;
	stats.wgsl_bytes += wgsl_bytes;
	stats.stages_baked += baked;
	stats.stages_unbaked += unbaked;
	stats.footer_parse_ms += double(OS::get_singleton()->get_ticks_usec() - parse_from) / 1000.0;

	return offset;
}

// =========================================================================
// Public API
// =========================================================================

bool RenderingShaderContainerWebGPU::get_wgsl_code(uint32_t p_shader_index, const char *&r_wgsl, uint32_t &r_length) const {
	r_wgsl = nullptr;
	r_length = 0;

	if (p_shader_index < (uint32_t)wgsl_view.size() && wgsl_view[p_shader_index].length > 0) {
		const WGSLView &view = wgsl_view[p_shader_index];
		// Bounds-checked rather than trusted: the length prefix comes off disk,
		// and a truncated or tampered container must not hand out a window past
		// the end of the buffer.
		if ((uint64_t)view.offset + view.length <= (uint64_t)source_bytes.size()) {
			r_wgsl = (const char *)source_bytes.ptr() + view.offset;
			r_length = view.length;
			return true;
		}
		return false;
	}

	if (p_shader_index < (uint32_t)wgsl_code.size() && !wgsl_code[p_shader_index].is_empty()) {
		r_wgsl = wgsl_code[p_shader_index].ptr();
		r_length = (uint32_t)wgsl_code[p_shader_index].length();
		return r_length > 0;
	}

	return false;
}

const Vector<spirv_lite_reflect::RawImageDecl> *RenderingShaderContainerWebGPU::get_baked_image_decls(uint32_t p_shader_index) const {
	if (!has_baked_image_decls() || p_shader_index >= (uint32_t)image_decls.size()) {
		return nullptr;
	}
	return &image_decls[p_shader_index];
}

bool RenderingShaderContainerWebGPU::get_stage_has_spec_constants(uint32_t p_shader_index) const {
	if (p_shader_index >= (uint32_t)stage_has_spec_constants_flags.size()) {
		return false;
	}
	return stage_has_spec_constants_flags[p_shader_index];
}

RenderingShaderContainerWebGPU::LoadStats &RenderingShaderContainerWebGPU::_load_stats() {
	static LoadStats stats;
	return stats;
}

const RenderingShaderContainerWebGPU::LoadStats &RenderingShaderContainerWebGPU::get_load_stats() {
	return _load_stats();
}

RenderingShaderContainerWebGPU::RenderingShaderContainerWebGPU() {
}

RenderingShaderContainerWebGPU::~RenderingShaderContainerWebGPU() {
}

#endif // WEBGPU_ENABLED || WEBGPU_SHADER_BAKER_ENABLED
