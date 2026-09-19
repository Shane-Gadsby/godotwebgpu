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
#include "wgsl_bake_subprocess.h"
#endif

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

bool RenderingShaderContainerWebGPU::_set_code_from_spirv(const ReflectShader &p_shader) {
	const uint32_t stage_count = p_shader.shader_stages.size();
	shaders.resize(stage_count);
	wgsl_code.resize(stage_count);

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
#else
		wgsl_code.write[i] = CharString();
#endif
	}

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
		uint32_t len = (i < wgsl_code.size()) ? (uint32_t)wgsl_code[i].length() : 0;
		if (p_bytes) {
			memcpy(p_bytes + offset, &len, sizeof(uint32_t));
		}
		offset += sizeof(uint32_t);
		if (len > 0) {
			if (p_bytes) {
				memcpy(p_bytes + offset, wgsl_code[i].ptr(), len);
			}
			offset += len;
		}
	}
	return offset;
}

uint32_t RenderingShaderContainerWebGPU::_from_bytes_footer_extra_data(const uint8_t *p_bytes) {
	uint32_t offset = 0;
	wgsl_code.resize(shaders.size());
	for (int i = 0; i < shaders.size(); i++) {
		uint32_t len = 0;
		memcpy(&len, p_bytes + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		if (len > 0) {
			CharString cs;
			cs.resize_uninitialized(len + 1);
			memcpy(cs.ptrw(), p_bytes + offset, len);
			cs.ptrw()[len] = '\0';
			wgsl_code.write[i] = cs;
			offset += len;
		} else {
			wgsl_code.write[i] = CharString();
		}
	}
	return offset;
}

// =========================================================================
// Public API
// =========================================================================

const char *RenderingShaderContainerWebGPU::get_wgsl_code(uint32_t p_shader_index) const {
	if (p_shader_index >= (uint32_t)wgsl_code.size() || wgsl_code[p_shader_index].is_empty()) {
		return nullptr;
	}
	return wgsl_code[p_shader_index].ptr();
}

RenderingShaderContainerWebGPU::RenderingShaderContainerWebGPU() {
}

RenderingShaderContainerWebGPU::~RenderingShaderContainerWebGPU() {
}

#endif // WEBGPU_ENABLED || WEBGPU_SHADER_BAKER_ENABLED
