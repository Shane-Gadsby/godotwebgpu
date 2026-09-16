/**************************************************************************/
/*  rendering_device_driver_webgpu.cpp                                    */
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

#ifdef WEBGPU_ENABLED

#include "rendering_device_driver_webgpu.h"

#include "core/templates/hash_map.h"
#include "core/templates/hashfuncs.h"
#include "drivers/webgpu/pixel_formats_webgpu.h"
#include "drivers/webgpu/rendering_context_driver_webgpu.h"
#include "drivers/webgpu/rendering_shader_container_webgpu.h"
#include "drivers/webgpu/spirv_preprocess.h"
#include "drivers/webgpu/tint_wrapper.h"

#include <emscripten/emscripten.h>
#include <webgpu/webgpu.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Define WEBGPU_VERBOSE to enable diagnostic console.log prints in the browser.
// These are disabled by default for production builds.
// #define WEBGPU_VERBOSE

#ifdef WEBGPU_VERBOSE
#define WEBGPU_DIAG(...) EM_ASM(__VA_ARGS__)
#define WEBGPU_DIAG_INT(...) EM_ASM_INT(__VA_ARGS__)
#else
#define WEBGPU_DIAG(...) ((void)0)
#define WEBGPU_DIAG_INT(...) 0
#endif

// Forward declaration for timestamp readback callback (defined below command_timestamp_query_pool_reset).
static void _timestamp_readback_callback(WGPUMapAsyncStatus p_status, WGPUStringView p_message, void *p_userdata1, void *p_userdata2);

// Fence work-done callback: fires when wgpuQueueSubmit work completes on GPU.
static void _fence_work_done_callback(WGPUQueueWorkDoneStatus p_status, void *p_userdata1, void *p_userdata2) {
	WGFence *fence = (WGFence *)p_userdata1;
	if (!fence) {
		return;
	}

	fence->work_done_pending = false;

	// Fence was freed while this callback was in flight — clean up.
	if (fence->freed) {
		delete fence;
		return;
	}

	fence->signaled = true;
}

// Parse "@group(G[u]) @binding(B[u])" from a WGSL string.
// Handles the optional 'u'/'i' suffix that Tint emits for integer literals.
// Returns true and sets r_grp/r_bnd on success.
static bool parse_group_binding(const char *p, unsigned int &r_grp, unsigned int &r_bnd) {
	if (strncmp(p, "@group(", 7) != 0) {
		return false;
	}
	const char *g = p + 7;
	char *end = nullptr;
	unsigned long grp = strtoul(g, &end, 10);
	if (end == g) {
		return false;
	}
	if (*end == 'u' || *end == 'i') {
		end++;
	}
	if (*end != ')') {
		return false;
	}
	end++; // skip ')'
	while (*end == ' ') {
		end++;
	}
	if (strncmp(end, "@binding(", 9) != 0) {
		return false;
	}
	const char *b = end + 9;
	unsigned long bnd = strtoul(b, &end, 10);
	if (end == b) {
		return false;
	}
	if (*end == 'u' || *end == 'i') {
		end++;
	}
	if (*end != ')') {
		return false;
	}
	r_grp = (unsigned int)grp;
	r_bnd = (unsigned int)bnd;
	return true;
}

// =============================================================================
// SPIR-V → WGSL conversion (Tint + SPIR-V preprocessing)
// =============================================================================
//
// emdawnwebgpu only accepts WGSL, so every shader stage must be converted from
// SPIR-V. Tint (C++, linked directly into the engine WASM) handles the
// conversion. Twelve SPIR-V preprocessing passes (push constant rewriting,
// sampler splitting, Y-flip, etc.) run before Tint's SpirvToWgsl.
//
// Cache lives for process lifetime — the SPIR-V → WGSL mapping is independent
// of the WebGPU device, and a single process never creates more than ~1k
// distinct shaders, so unbounded growth is fine in practice.
// Uses a 64-bit key (two MurmurHash3 passes with different seeds) to avoid
// birthday-paradox collisions that a 32-bit hash would risk at ~1k entries.
static HashMap<uint64_t, String> _spv_to_wgsl_cache;
static uint32_t _spv_to_wgsl_cache_hits = 0;
static uint32_t _spv_to_wgsl_cache_misses = 0;
static uint32_t _spv_to_wgsl_precompiled_hits = 0;

// Build-time precompiled WGSL lookup table (generated by wgsl_precompile.py).
// Sorted by hash for binary search.
#include "drivers/webgpu/wgsl_precompiled.gen.h"

// Binary search the precompiled table for a matching SPIR-V hash.
static const char *_lookup_precompiled_wgsl(uint64_t p_spv_hash) {
	if (_wgsl_precompiled_count == 0) {
		return nullptr;
	}
	uint32_t lo = 0;
	uint32_t hi = _wgsl_precompiled_count;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (_wgsl_precompiled[mid].spv_hash < p_spv_hash) {
			lo = mid + 1;
		} else if (_wgsl_precompiled[mid].spv_hash > p_spv_hash) {
			hi = mid;
		} else {
			return _wgsl_precompiled[mid].wgsl;
		}
	}
	return nullptr;
}

// Run SPIR-V preprocessing passes and translate to WGSL via Tint.
// Returns a malloc'd null-terminated WGSL string, or nullptr on failure.
static char *_translate_spirv_to_wgsl(const uint8_t *p_spv_ptr, int p_spv_size) {
	if (p_spv_size < 20 || (p_spv_size % 4) != 0) {
		return nullptr;
	}

	// Wrap raw bytes in a Vector for the preprocessing API.
	Vector<uint8_t> spv;
	spv.resize(p_spv_size);
	memcpy(spv.ptrw(), p_spv_ptr, p_spv_size);

	// SPIR-V preprocessing pipeline:
	spv = spirv_preprocess::inline_opaque_functions(spv);
	spv = spirv_preprocess::freeze_spec_constant_ops(spv);
	spv = spirv_preprocess::rewrite_copy_logical(spv);
	spv = spirv_preprocess::rewrite_terminate_invocation(spv);
	spv = spirv_preprocess::convert_push_constants_to_uniforms(spv);
	spv = spirv_preprocess::split_combined_samplers(spv);
	auto depth_result = spirv_preprocess::fix_depth2_images(spv);
	spv = depth_result.bytes;
	spv = spirv_preprocess::negate_position_y(spv);
	spv = spirv_preprocess::strip_unsupported_decorations(spv);
	spv = spirv_preprocess::strip_memory_barrier(spv);
	spv = spirv_preprocess::strip_helper_invocation_builtin(spv);
	spv = spirv_preprocess::fold_ballot_bit_count(spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);
	spv = spirv_preprocess::strip_writeonly_storage_decoration(spv);
	// Runs last: strips resource globals unused by this stage's entry point
	// (see spirv_preprocess.h for why — this is what makes the per-stage
	// WGSL scan below actually narrow BGL visibility instead of seeing every
	// shared-include declaration as "used by every stage").
	spv = spirv_preprocess::eliminate_dead_resources(spv);
	spv = spirv_preprocess::eliminate_local_single_block_vars(spv);

	// Convert to uint32_t words for Tint.
	int word_count = spv.size() / 4;
	std::vector<uint32_t> spirv_words(word_count);
	memcpy(spirv_words.data(), spv.ptr(), spv.size());

	// Call Tint SPIR-V → WGSL via the wrapper (isolates C++20 from Godot's build).
	char *error_msg = nullptr;
	char *out = tint_wrapper_spirv_to_wgsl(spirv_words.data(), spirv_words.size(), &error_msg);
	if (!out) {
		if (error_msg) {
			// Truncate the error message: Tint includes the full SPIR-V disassembly
			// which floods the console. Keep first 1500 chars for diagnostics.
			String err_str = String::utf8(error_msg);
			// Replace newlines with pipes to prevent console splitting.
			err_str = err_str.replace("\n", " | ");
			if (err_str.length() > 1500) {
				err_str = err_str.left(1500) + "... [truncated]";
			}
			ERR_PRINT(vformat("Tint SPIR-V→WGSL failed: %s", err_str));
			free(error_msg);
		} else {
			ERR_PRINT("Tint SPIR-V→WGSL failed (unknown error)");
		}
		return nullptr;
	}
	return out;
}

// Returns a malloc'd null-terminated WGSL string (caller must free), or nullptr on
// failure. Checks: (1) in-memory cache, (2) precompiled table, (3) Tint fallback.
static char *_spv_to_wgsl_cached(const uint8_t *p_spv_ptr, int p_spv_size) {
	uint32_t hash_lo = hash_murmur3_buffer(p_spv_ptr, p_spv_size);
	uint32_t hash_hi = hash_murmur3_buffer(p_spv_ptr, p_spv_size, 0x9E3779B9);
	uint64_t spv_hash = ((uint64_t)hash_hi << 32) | hash_lo;

	// 1. Check in-memory cache (hits on repeated calls within same session).
	const String *cached = _spv_to_wgsl_cache.getptr(spv_hash);
	if (cached) {
		_spv_to_wgsl_cache_hits++;
		CharString cs = cached->utf8();
		size_t len = (size_t)cs.length() + 1;
		char *out = (char *)malloc(len);
		if (!out) {
			return nullptr;
		}
		memcpy(out, cs.get_data(), len);
		return out;
	}

	// 2. Check build-time precompiled table (eliminates runtime translation for ubershaders).
	const char *precompiled = _lookup_precompiled_wgsl(spv_hash);
	if (precompiled) {
		_spv_to_wgsl_precompiled_hits++;
		WEBGPU_DIAG({
			if ($0 <= 5 || ($0 % 50) === 0) {
				console.log('[SHADER] Precompiled WGSL hit #' + $0);
			} }, _spv_to_wgsl_precompiled_hits);
		size_t len = strlen(precompiled) + 1;
		char *out = (char *)malloc(len);
		if (!out) {
			return nullptr;
		}
		memcpy(out, precompiled, len);
		_spv_to_wgsl_cache[spv_hash] = String(precompiled);
		return out;
	}

	// 3. Fall back to Tint (for specialized shaders and shaders not in the table).
	_spv_to_wgsl_cache_misses++;

	char *wgsl_str = _translate_spirv_to_wgsl(p_spv_ptr, p_spv_size);

	if (wgsl_str) {
		_spv_to_wgsl_cache[spv_hash] = String(wgsl_str);
	}

	WEBGPU_DIAG({
		if ($2 <= 5 || ($2 % 50) === 0) {
			console.log('[SHADER] WGSL stats: precompiled_hits=' + $0 + ' cache_hits=' + $1 + ' tint_misses=' + $2);
		} }, _spv_to_wgsl_precompiled_hits, _spv_to_wgsl_cache_hits, _spv_to_wgsl_cache_misses);

	return wgsl_str;
}

// =============================================================================
// Storage Texture Format Validation
// =============================================================================

// Returns true if the WebGPU texture format is a depth or depth/stencil format.
static bool _is_depth_format(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_Depth16Unorm:
		case WGPUTextureFormat_Depth24Plus:
		case WGPUTextureFormat_Depth24PlusStencil8:
		case WGPUTextureFormat_Depth32Float:
		case WGPUTextureFormat_Depth32FloatStencil8:
			return true;
		default:
			return false;
	}
}

// Returns true for 32-bit float formats that require the float32-filterable
// feature for linear sampling (R32Float, RG32Float, RGBA32Float).
static bool _is_float32_format(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_R32Float:
		case WGPUTextureFormat_RG32Float:
		case WGPUTextureFormat_RGBA32Float:
			return true;
		default:
			return false;
	}
}

// Maps a WGSL storage-texel-format identifier (e.g. "rgba16sint", as it
// appears in a texture_storage_2d<fmt, access> declaration) to its
// WGPUTextureFormat. Shared by every place that needs to know a storage
// texture's real format after its WGSL declaration text is gone (the
// read-only-storage-to-sampled conversion, and the read_write-storage
// shadow-read split) -- both record this eagerly, before their rewrite
// removes the declaration a later text scan could otherwise read it from.
static WGPUTextureFormat _wgsl_storage_format_string_to_wgpu(const String &p_fmt) {
	if (p_fmt == "rgba8unorm") {
		return WGPUTextureFormat_RGBA8Unorm;
	}
	if (p_fmt == "rgba8snorm") {
		return WGPUTextureFormat_RGBA8Snorm;
	}
	if (p_fmt == "rgba8uint") {
		return WGPUTextureFormat_RGBA8Uint;
	}
	if (p_fmt == "rgba8sint") {
		return WGPUTextureFormat_RGBA8Sint;
	}
	if (p_fmt == "rgba16float") {
		return WGPUTextureFormat_RGBA16Float;
	}
	if (p_fmt == "rgba16uint") {
		return WGPUTextureFormat_RGBA16Uint;
	}
	if (p_fmt == "rgba16sint") {
		return WGPUTextureFormat_RGBA16Sint;
	}
	if (p_fmt == "rgba32float") {
		return WGPUTextureFormat_RGBA32Float;
	}
	if (p_fmt == "rgba32uint") {
		return WGPUTextureFormat_RGBA32Uint;
	}
	if (p_fmt == "rgba32sint") {
		return WGPUTextureFormat_RGBA32Sint;
	}
	if (p_fmt == "rg32float") {
		return WGPUTextureFormat_RG32Float;
	}
	if (p_fmt == "rg32uint") {
		return WGPUTextureFormat_RG32Uint;
	}
	if (p_fmt == "rg32sint") {
		return WGPUTextureFormat_RG32Sint;
	}
	if (p_fmt == "r32float") {
		return WGPUTextureFormat_R32Float;
	}
	if (p_fmt == "r32uint") {
		return WGPUTextureFormat_R32Uint;
	}
	if (p_fmt == "r32sint") {
		return WGPUTextureFormat_R32Sint;
	}
	if (p_fmt == "r8unorm") {
		return WGPUTextureFormat_R8Unorm;
	}
	if (p_fmt == "r8snorm") {
		return WGPUTextureFormat_R8Snorm;
	}
	if (p_fmt == "r8uint") {
		return WGPUTextureFormat_R8Uint;
	}
	if (p_fmt == "r8sint") {
		return WGPUTextureFormat_R8Sint;
	}
	if (p_fmt == "rg8unorm") {
		return WGPUTextureFormat_RG8Unorm;
	}
	if (p_fmt == "rg8snorm") {
		return WGPUTextureFormat_RG8Snorm;
	}
	if (p_fmt == "rg8uint") {
		return WGPUTextureFormat_RG8Uint;
	}
	if (p_fmt == "rg8sint") {
		return WGPUTextureFormat_RG8Sint;
	}
	if (p_fmt == "bgra8unorm") {
		return WGPUTextureFormat_BGRA8Unorm;
	}
	if (p_fmt == "rgb10a2unorm") {
		return WGPUTextureFormat_RGB10A2Unorm;
	}
	if (p_fmt == "rgb10a2uint") {
		return WGPUTextureFormat_RGB10A2Uint;
	}
	if (p_fmt == "rg11b10ufloat") {
		return WGPUTextureFormat_RG11B10Ufloat;
	}
	return WGPUTextureFormat_RGBA8Unorm; // fallback
}

// Maps a SPIR-V `ImageFormat` operand (see the SPIR-V spec's "Image Format"
// enum -- this is the literal numeric encoding glslang emits into
// `OpTypeImage`'s Format operand, e.g. R8=15 for GLSL's `layout(r8, ...)`) to
// the equivalent WGPUTextureFormat. Numeric counterpart to
// _wgsl_storage_format_string_to_wgpu() above, for use directly on raw
// SPIR-V -- see _extract_pre_dce_storage_image_info()'s doc comment for why a
// second, SPIR-V-level source of this same information is needed at all.
// Covers the same format set as the string-based table (plus the signed/
// unsigned integer variants that only arise from raw SPIR-V, never from a
// WGSL text scan, since Tint spells those out fully in the type name it
// emits, e.g. "rg8sint" already has a direct string match above).
static WGPUTextureFormat _spirv_image_format_to_wgpu(uint32_t p_spirv_format) {
	switch (p_spirv_format) {
		case 1:
			return WGPUTextureFormat_RGBA32Float; // Rgba32f
		case 2:
			return WGPUTextureFormat_RGBA16Float; // Rgba16f
		case 3:
			return WGPUTextureFormat_R32Float; // R32f
		case 4:
			return WGPUTextureFormat_RGBA8Unorm; // Rgba8
		case 5:
			return WGPUTextureFormat_RGBA8Snorm; // Rgba8Snorm
		case 6:
			return WGPUTextureFormat_RG32Float; // Rg32f
		case 7:
			return WGPUTextureFormat_RG16Float; // Rg16f
		case 8:
			return WGPUTextureFormat_RG11B10Ufloat; // R11fG11fB10f
		case 9:
			return WGPUTextureFormat_R16Float; // R16f
		case 10:
			return WGPUTextureFormat_RGBA16Float; // Rgba16 (unorm16, no WGPU equivalent -- see _promote_storage_format's own R16Unorm fallback)
		case 11:
			return WGPUTextureFormat_RGB10A2Unorm; // Rgb10A2
		case 12:
			return WGPUTextureFormat_RG16Float; // Rg16 (unorm16 fallback, matches _promote_storage_format)
		case 13:
			return WGPUTextureFormat_RG8Unorm; // Rg8
		case 14:
			return WGPUTextureFormat_R16Float; // R16 (unorm16 fallback, matches _promote_storage_format)
		case 15:
			return WGPUTextureFormat_R8Unorm; // R8
		case 16:
			return WGPUTextureFormat_RGBA16Float; // Rgba16Snorm (snorm16 fallback)
		case 17:
			return WGPUTextureFormat_RG16Float; // Rg16Snorm (snorm16 fallback)
		case 18:
			return WGPUTextureFormat_RG8Snorm; // Rg8Snorm
		case 19:
			return WGPUTextureFormat_R16Float; // R16Snorm (snorm16 fallback)
		case 20:
			return WGPUTextureFormat_R8Snorm; // R8Snorm
		case 21:
			return WGPUTextureFormat_RGBA32Sint; // Rgba32i
		case 22:
			return WGPUTextureFormat_RGBA16Sint; // Rgba16i
		case 23:
			return WGPUTextureFormat_RGBA8Sint; // Rgba8i
		case 24:
			return WGPUTextureFormat_R32Sint; // R32i
		case 25:
			return WGPUTextureFormat_RG32Sint; // Rg32i
		case 26:
			return WGPUTextureFormat_RG16Sint; // Rg16i
		case 27:
			return WGPUTextureFormat_RG8Sint; // Rg8i
		case 28:
			return WGPUTextureFormat_R16Sint; // R16i
		case 29:
			return WGPUTextureFormat_R8Sint; // R8i
		case 30:
			return WGPUTextureFormat_RGBA32Uint; // Rgba32ui
		case 31:
			return WGPUTextureFormat_RGBA16Uint; // Rgba16ui
		case 32:
			return WGPUTextureFormat_RGBA8Uint; // Rgba8ui
		case 33:
			return WGPUTextureFormat_R32Uint; // R32ui
		case 34:
			return WGPUTextureFormat_RGB10A2Uint; // Rgb10a2ui
		case 35:
			return WGPUTextureFormat_RG32Uint; // Rg32ui
		case 36:
			return WGPUTextureFormat_RG16Uint; // Rg16ui
		case 37:
			return WGPUTextureFormat_RG8Uint; // Rg8ui
		case 38:
			return WGPUTextureFormat_R16Uint; // R16ui
		case 39:
			return WGPUTextureFormat_R8Uint; // R8ui
		default:
			return WGPUTextureFormat_Undefined; // Unknown (0), R64ui/R64i (40/41, unsupported), or unrecognized.
	}
}

// Scans raw (unpreprocessed) SPIR-V directly -- *before* eliminate_dead_resources()
// (Task 8.7's AggressiveDCE pass) gets a chance to run -- for every storage-image
// (`OpTypeImage` behind a `UniformConstant`-storage-class `OpVariable`) binding's
// declared texel format, keyed by (set << 16 | binding).
//
// Why this needs to exist as a *second*, independent source of truth alongside
// the various post-Tint WGSL-text scans elsewhere in this file (all of which,
// unlike this one, run on the WGSL Tint emits *after* our own DCE pass has
// already stripped anything unreachable from this stage's actual entry point):
// a storage image can be declared in the GLSL/SPIR-V but never referenced in
// one particular compiled shader *variant* (different variants of the same
// ShaderRD share the same uniform interface but not the same control flow --
// see ssil_interleave.glsl's `source_edges`, used by variant 1 but genuinely
// unreachable from variant 0's own entry point). glslang's SPIR-V output
// always declares every top-level uniform regardless of which #ifdef branch
// of `main()` reads it, and Godot's own engine-side shader reflection
// (built from that same un-DCE'd SPIR-V, shared by every RD backend) matches
// that -- so `ss_effects.cpp`'s C++ code unconditionally builds a real
// R8_UNORM texture uniform for this binding on every variant, and this
// driver's BindGroupLayout-building code (driven by that same reflection,
// not by what Tint's *output* still contains) still creates a real
// `storageTexture` entry for it too. But once *our* DCE pass correctly
// strips the binding from a variant that never reaches it, there is nothing
// left in the WGSL for the post-Tint text scans to find a format string in --
// they come up empty, and the BGL entry silently defaults to RGBA8Unorm,
// mismatching whatever format the real texture actually is
// ("Format (R8Unorm) of [Texture] expected to be (RGBA8Unorm)"). A texture's
// declared *format* (unlike, say, a sampler's comparison-ness) is type-level
// information present in the declaration itself, independent of whether the
// declaration is ever used -- so, unlike that sampler-comparison problem, it
// *can* be recovered here, by reading it before DCE has a chance to remove
// the declaration that carries it.
//
// This does not need to run through the rest of the preprocessing pipeline
// (specialization constants, push-constant conversion, etc. -- none of that
// affects a plain OpTypeImage/OpVariable/OpDecorate triad), so it operates
// directly on the same raw per-stage SPIR-V bytes shader_create_from_container()
// already has on hand, independent of (and safely callable regardless of)
// whether the eventual WGSL for this stage comes from a fresh Tint run or the
// build-time precompiled cache.
struct PreDceImageInfo {
	WGPUTextureFormat format = WGPUTextureFormat_Undefined;
	WGPUTextureViewDimension dim = WGPUTextureViewDimension_Undefined;
	// Uint/Sint for a GLSL utexture*/itexture* (SPIR-V OpTypeImage's SampledType
	// operand resolves to an OpTypeInt), Undefined otherwise -- deliberately NOT
	// populated as Float for a genuine float sampled type (see call site: the
	// existing default of Float is already correct for that case, so only the
	// Uint/Sint case needs recovering here, same reasoning as the format/dim
	// fields above but for RDD::UNIFORM_TYPE_TEXTURE's sampleType instead of a
	// storage image's format).
	WGPUTextureSampleType sample_type = WGPUTextureSampleType_Undefined;
};

static HashMap<uint32_t, PreDceImageInfo> _extract_pre_dce_storage_image_info(const uint8_t *p_spv_ptr, int p_spv_size) {
	HashMap<uint32_t, PreDceImageInfo> result;
	if (p_spv_size < 20 || (p_spv_size % 4) != 0) {
		return result;
	}
	const uint32_t *words = (const uint32_t *)p_spv_ptr;
	uint32_t word_count = (uint32_t)(p_spv_size / 4);
	if (words[0] != 0x07230203u) {
		return result; // Not a valid SPIR-V magic number.
	}

	// First pass: collect the small amount of type/decoration info needed,
	// keyed by SPIR-V result <id>.
	struct ImageTypeInfo {
		uint32_t format = 0; // SPIR-V ImageFormat operand.
		uint32_t dim = 0; // SPIR-V Dim operand (0=1D, 1=2D, 2=3D, 3=Cube, ...).
		uint32_t arrayed = 0;
		uint32_t sampled_type_id = 0; // SPIR-V <id> of the component (Sampled Type) operand.
	};
	HashMap<uint32_t, ImageTypeInfo> image_types; // image type id -> info
	HashMap<uint32_t, bool> int_type_signed; // OpTypeInt result id -> Signedness (true = signed)
	HashMap<uint32_t, uint32_t> pointer_pointee; // UniformConstant pointer type id -> pointee type id
	HashMap<uint32_t, uint32_t> var_pointer_type; // UniformConstant variable id -> its pointer type id
	HashMap<uint32_t, uint32_t> decorate_set; // target id -> DescriptorSet value
	HashMap<uint32_t, uint32_t> decorate_binding; // target id -> Binding value
	// array type id -> element type id. A GLSL array-of-textures uniform
	// (e.g. gi.glsl's "texture3D sdf_cascades[SDFGI_MAX_CASCADES]") declares
	// its UniformConstant pointer's pointee as an OpTypeArray/OpTypeRuntimeArray
	// wrapping the real OpTypeImage, not the image type directly -- without
	// unwrapping this, the second pass below's `image_types.getptr(*pointee)`
	// always misses for such a binding, silently skipping it (see below).
	HashMap<uint32_t, uint32_t> array_to_elem;

	uint32_t pos = 5; // Skip the 5-word header.
	while (pos < word_count) {
		uint32_t inst_word0 = words[pos];
		uint32_t inst_len = inst_word0 >> 16;
		uint32_t opcode = inst_word0 & 0xFFFFu;
		if (inst_len == 0 || pos + inst_len > word_count) {
			break; // Malformed/truncated -- bail out safely with whatever was already found.
		}
		switch (opcode) {
			case 71: // OpDecorate: <id>Target, Decoration, [operands...]
				if (inst_len >= 4) {
					uint32_t target = words[pos + 1];
					uint32_t decoration = words[pos + 2];
					if (decoration == 34) { // DescriptorSet
						decorate_set[target] = words[pos + 3];
					} else if (decoration == 33) { // Binding
						decorate_binding[target] = words[pos + 3];
					}
				}
				break;
			case 25: // OpTypeImage: <id>Result, <id>SampledType, Dim, Depth, Arrayed, MS, Sampled, Format, [AccessQualifier]
				if (inst_len >= 9) {
					uint32_t result_id = words[pos + 1];
					ImageTypeInfo info;
					info.sampled_type_id = words[pos + 2];
					info.dim = words[pos + 3];
					info.arrayed = words[pos + 5];
					info.format = words[pos + 8];
					image_types[result_id] = info;
				}
				break;
			case 21: // OpTypeInt: <id>Result, Width, Signedness
				if (inst_len >= 4) {
					int_type_signed[words[pos + 1]] = words[pos + 3] != 0;
				}
				break;
			case 32: // OpTypePointer: <id>Result, StorageClass, <id>Type
				if (inst_len >= 4) {
					uint32_t storage_class = words[pos + 2];
					if (storage_class == 0) { // UniformConstant
						uint32_t result_id = words[pos + 1];
						uint32_t pointee = words[pos + 3];
						pointer_pointee[result_id] = pointee;
					}
				}
				break;
			case 28: // OpTypeArray: <id>Result, <id>ElementType, <id>Length
				if (inst_len >= 3) {
					array_to_elem[words[pos + 1]] = words[pos + 2];
				}
				break;
			case 29: // OpTypeRuntimeArray: <id>Result, <id>ElementType
				if (inst_len >= 3) {
					array_to_elem[words[pos + 1]] = words[pos + 2];
				}
				break;
			case 59: // OpVariable: <id>ResultType, <id>Result, StorageClass, [Initializer]
				if (inst_len >= 4) {
					uint32_t storage_class = words[pos + 3];
					if (storage_class == 0) { // UniformConstant
						uint32_t result_type = words[pos + 1];
						uint32_t result_id = words[pos + 2];
						var_pointer_type[result_id] = result_type;
					}
				}
				break;
			default:
				break;
		}
		pos += inst_len;
	}

	// Second pass: resolve each UniformConstant image variable to its
	// (set, binding) -> (format, dimension) info.
	for (const KeyValue<uint32_t, uint32_t> &kv : var_pointer_type) {
		uint32_t var_id = kv.key;
		uint32_t ptr_type_id = kv.value;
		const uint32_t *pointee = pointer_pointee.getptr(ptr_type_id);
		if (!pointee) {
			continue;
		}
		// A GLSL array-of-textures uniform's pointee is an OpTypeArray/
		// OpTypeRuntimeArray wrapping the element image type, not the image
		// type directly -- unwrap it (one level is sufficient; SPIR-V/GLSL
		// don't nest arrays of opaque handle types) before the image-type
		// lookup below, or every such binding is silently skipped.
		uint32_t elem_type_id = *pointee;
		if (const uint32_t *arr_elem = array_to_elem.getptr(elem_type_id)) {
			elem_type_id = *arr_elem;
		}
		const ImageTypeInfo *img = image_types.getptr(elem_type_id);
		if (!img) {
			continue; // Not an image type (e.g. a sampler or combined-image-sampler variable).
		}
		const uint32_t *set = decorate_set.getptr(var_id);
		const uint32_t *binding = decorate_binding.getptr(var_id);
		if (!set || !binding) {
			continue;
		}
		PreDceImageInfo info;
		info.format = _spirv_image_format_to_wgpu(img->format);
		// SPIR-V Dim: 0=1D, 1=2D, 2=3D, 3=Cube, 4=Rect, 5=Buffer, 6=SubpassData.
		switch (img->dim) {
			case 0:
				info.dim = WGPUTextureViewDimension_1D;
				break;
			case 1:
				info.dim = img->arrayed ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
				break;
			case 2:
				info.dim = WGPUTextureViewDimension_3D;
				break;
			case 3:
				info.dim = img->arrayed ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_Cube;
				break;
			default:
				info.dim = WGPUTextureViewDimension_Undefined;
				break;
		}
		// Only an integer component type needs recovering here -- Float is
		// already the correct fallback default everywhere info.sample_type is
		// consulted, so a genuine float image (or one we can't resolve, e.g.
		// SampledType id not found in int_type_signed) is deliberately left
		// as Undefined rather than asserted to Float.
		if (const bool *is_signed = int_type_signed.getptr(img->sampled_type_id)) {
			info.sample_type = *is_signed ? WGPUTextureSampleType_Sint : WGPUTextureSampleType_Uint;
		}
		if (info.format == WGPUTextureFormat_Undefined && info.dim == WGPUTextureViewDimension_Undefined &&
				info.sample_type == WGPUTextureSampleType_Undefined) {
			continue; // Nothing usable -- leave it for the WGSL-text scans to handle normally.
		}
		// split_combined_samplers() (spirv_preprocess.cpp) doubles every
		// non-combined binding's number (image2D/image3D storage bindings are
		// never "combined" -- that only applies to sampler2D-style GLSL types)
		// when it runs, later in the pipeline, on the *preprocessed* copy of
		// this same SPIR-V -- but this function reads the raw bytes before any
		// of that happens, so it still sees the original, undoubled GLSL
		// binding number. Every other populator of wgsl_storage_tex_format /
		// wgsl_tex_dims keys on the final, doubled WGSL binding (it's reading
		// @binding(B) directly out of Tint's output), so this key must double
		// it too to land in the same key space.
		uint32_t key = ((*set) << 16) | ((*binding) * 2);
		result[key] = info;
	}
	return result;
}

// Maps a WGPUTextureFormat to the WGPUTextureSampleType needed for a sampled
// texture BGL entry. Integer formats → Uint/Sint, everything else → UnfilterableFloat.
static WGPUTextureSampleType _texture_sample_type_for_format(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_R32Uint:
		case WGPUTextureFormat_RGBA8Uint:
		case WGPUTextureFormat_RG32Uint:
		case WGPUTextureFormat_RGBA16Uint:
		case WGPUTextureFormat_RGBA32Uint:
			return WGPUTextureSampleType_Uint;
		case WGPUTextureFormat_R32Sint:
		case WGPUTextureFormat_RGBA8Sint:
		case WGPUTextureFormat_RG32Sint:
		case WGPUTextureFormat_RGBA16Sint:
		case WGPUTextureFormat_RGBA32Sint:
			return WGPUTextureSampleType_Sint;
		default:
			return WGPUTextureSampleType_UnfilterableFloat;
	}
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

RenderingDeviceDriverWebGPU::RenderingDeviceDriverWebGPU(RenderingContextDriverWebGPU *p_context_driver) {
	context_driver = p_context_driver;
}

RenderingDeviceDriverWebGPU::~RenderingDeviceDriverWebGPU() {
	// Release push constant resources.
	if (push_constant_bind_group) {
		wgpuBindGroupRelease(push_constant_bind_group);
		push_constant_bind_group = nullptr;
	}
	if (push_constant_bind_group_layout) {
		wgpuBindGroupLayoutRelease(push_constant_bind_group_layout);
		push_constant_bind_group_layout = nullptr;
	}
	if (push_constant_ring_buffer) {
		wgpuBufferRelease(push_constant_ring_buffer);
		push_constant_ring_buffer = nullptr;
	}
	if (empty_bind_group) {
		wgpuBindGroupRelease(empty_bind_group);
		empty_bind_group = nullptr;
	}
	if (empty_bind_group_layout) {
		wgpuBindGroupLayoutRelease(empty_bind_group_layout);
		empty_bind_group_layout = nullptr;
	}

	// Release fallback textures and views.
	if (fallback_float_texture_view) {
		wgpuTextureViewRelease(fallback_float_texture_view);
		fallback_float_texture_view = nullptr;
	}
	if (fallback_float_texture) {
		wgpuTextureRelease(fallback_float_texture);
		fallback_float_texture = nullptr;
	}
	if (fallback_cube_texture_view) {
		wgpuTextureViewRelease(fallback_cube_texture_view);
		fallback_cube_texture_view = nullptr;
	}
	if (fallback_cube_texture) {
		wgpuTextureRelease(fallback_cube_texture);
		fallback_cube_texture = nullptr;
	}
	if (fallback_ms_texture_view) {
		wgpuTextureViewRelease(fallback_ms_texture_view);
		fallback_ms_texture_view = nullptr;
	}
	if (fallback_ms_texture) {
		wgpuTextureRelease(fallback_ms_texture);
		fallback_ms_texture = nullptr;
	}

	// Release dummy samplers.
	if (dummy_filtering_sampler) {
		wgpuSamplerRelease(dummy_filtering_sampler);
		dummy_filtering_sampler = nullptr;
	}
	if (dummy_comparison_sampler) {
		wgpuSamplerRelease(dummy_comparison_sampler);
		dummy_comparison_sampler = nullptr;
	}
	if (dummy_nonfiltering_sampler) {
		wgpuSamplerRelease(dummy_nonfiltering_sampler);
		dummy_nonfiltering_sampler = nullptr;
	}

	// Release aliasing stub buffer.
	if (aliasing_stub_buffer) {
		wgpuBufferRelease(aliasing_stub_buffer);
		aliasing_stub_buffer = nullptr;
	}

	// Clean up readback cache — release persistent staging buffers and shadow memory.
	for (KeyValue<uint64_t, ReadbackEntry *> &kv : _readback_cache) {
		ReadbackEntry *entry = kv.value;
		if (entry->staging) {
			wgpuBufferRelease(entry->staging);
		}
		if (entry->shadow) {
			memfree(entry->shadow);
		}
		memdelete(entry);
	}
	_readback_cache.clear();

	if (shader_container_format) {
		memdelete(shader_container_format);
		shader_container_format = nullptr;
	}
}

// =============================================================================
// GENERIC
// =============================================================================

Error RenderingDeviceDriverWebGPU::initialize(uint32_t p_device_index, uint32_t p_frame_count) {
	// Initialize Tint shader compiler (SPIR-V → WGSL).
	tint_wrapper_initialize();

	device = context_driver->get_device();
	queue = context_driver->get_queue();
	ERR_FAIL_COND_V(device == nullptr, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(queue == nullptr, ERR_CANT_CREATE);

	frame_count = p_frame_count;

	// Query device limits.
	_check_capabilities();

	// Create push constant ring buffer.
	{
		WGPUBufferDescriptor desc = {};
		desc.size = PUSH_CONSTANT_RING_SIZE;
		desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
		desc.mappedAtCreation = false;
		push_constant_ring_buffer = wgpuDeviceCreateBuffer(device, &desc);
		ERR_FAIL_COND_V(push_constant_ring_buffer == nullptr, ERR_CANT_CREATE);
	}

	// Create a universal push constant bind group layout:
	//   PUSH_CONSTANT_RING_BINDING, all stages, uniform buffer with dynamic offset.
	// All shaders with push constants use this same layout for their push
	// constant slot in the pipeline layout (hasDynamicOffset=true allows
	// reusing one bind group with different ring buffer offsets per draw).
	{
		WGPUBindGroupLayoutEntry pc_entry = {};
		pc_entry.binding = PUSH_CONSTANT_RING_BINDING;
		pc_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
		pc_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
		pc_entry.buffer.hasDynamicOffset = true;
		pc_entry.buffer.minBindingSize = 0; // Flexible — works for any push constant size.

		WGPUBindGroupLayoutDescriptor layout_desc = {};
		layout_desc.entryCount = 1;
		layout_desc.entries = &pc_entry;
		push_constant_bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);
		ERR_FAIL_COND_V(push_constant_bind_group_layout == nullptr, ERR_CANT_CREATE);

		// Create the bind group (backed by the ring buffer).
		// Dynamic offset is applied at bind time via SetBindGroup(..., 1, &offset).
		WGPUBindGroupEntry bg_entry = {};
		bg_entry.binding = PUSH_CONSTANT_RING_BINDING;
		bg_entry.buffer = push_constant_ring_buffer;
		bg_entry.offset = 0;
		bg_entry.size = PUSH_CONSTANT_SLOT_ALIGNMENT;

		WGPUBindGroupDescriptor bg_desc = {};
		bg_desc.layout = push_constant_bind_group_layout;
		bg_desc.entryCount = 1;
		bg_desc.entries = &bg_entry;
		push_constant_bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);
		ERR_FAIL_COND_V(push_constant_bind_group == nullptr, ERR_CANT_CREATE);
	}

	// Create a persistent empty bind group for pipeline layout gaps.
	// Firefox/wgpu requires all bind group slots to be set before draw calls,
	// even if the BGL at that slot has zero entries.
	{
		WGPUBindGroupLayoutDescriptor empty_desc = {};
		empty_desc.entryCount = 0;
		empty_bind_group_layout = wgpuDeviceCreateBindGroupLayout(device, &empty_desc);
		ERR_FAIL_COND_V(empty_bind_group_layout == nullptr, ERR_CANT_CREATE);

		WGPUBindGroupDescriptor bg_desc = {};
		bg_desc.layout = empty_bind_group_layout;
		bg_desc.entryCount = 0;
		empty_bind_group = wgpuDeviceCreateBindGroup(device, &bg_desc);
		ERR_FAIL_COND_V(empty_bind_group == nullptr, ERR_CANT_CREATE);
	}

	// Create a small fallback float texture (4x4, RGBA8Unorm, all zeros).
	// Used when Godot provides a depth-format fallback texture to a BGL entry
	// that expects Float sample type. WebGPU requires format/sample-type match.
	{
		WGPUTextureDescriptor td = {};
		td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
		td.dimension = WGPUTextureDimension_2D;
		td.size = { 4, 4, 1 };
		td.format = WGPUTextureFormat_RGBA8Unorm;
		td.mipLevelCount = 1;
		td.sampleCount = 1;
		fallback_float_texture = wgpuDeviceCreateTexture(device, &td);
		if (fallback_float_texture) {
			WGPUTextureViewDescriptor vd = {};
			vd.format = WGPUTextureFormat_RGBA8Unorm;
			vd.dimension = WGPUTextureViewDimension_2D;
			vd.mipLevelCount = 1;
			vd.arrayLayerCount = 1;
			vd.aspect = WGPUTextureAspect_All;
			fallback_float_texture_view = wgpuTextureCreateView(fallback_float_texture, &vd);
		}
	}

	// Create fallback cube texture (4x4, 6 layers) for BGL entries expecting Cube views.
	{
		WGPUTextureDescriptor td = {};
		td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
		td.dimension = WGPUTextureDimension_2D;
		td.size = { 4, 4, 6 };
		td.format = WGPUTextureFormat_RGBA8Unorm;
		td.mipLevelCount = 1;
		td.sampleCount = 1;
		fallback_cube_texture = wgpuDeviceCreateTexture(device, &td);
		if (fallback_cube_texture) {
			WGPUTextureViewDescriptor vd = {};
			vd.format = WGPUTextureFormat_RGBA8Unorm;
			vd.dimension = WGPUTextureViewDimension_Cube;
			vd.mipLevelCount = 1;
			vd.arrayLayerCount = 6;
			vd.aspect = WGPUTextureAspect_All;
			fallback_cube_texture_view = wgpuTextureCreateView(fallback_cube_texture, &vd);
		}
	}

	// Create fallback multisampled texture (4x4, 4x MSAA, RenderAttachment usage
	// required for MSAA textures). Used by ResolveRasterShaderRD and similar MSAA
	// depth resolve shaders when the source is a depth format (can't be sampled
	// as UnfilterableFloat in WebGPU). The shader reads zeros but no GPU errors.
	{
		WGPUTextureDescriptor td = {};
		// MSAA textures require RenderAttachment usage; we never render to this
		// texture but the usage flag is still required by the WebGPU spec.
		td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
		td.dimension = WGPUTextureDimension_2D;
		td.size = { 4, 4, 1 };
		td.format = WGPUTextureFormat_R32Float; // Non-filterable float.
		td.mipLevelCount = 1;
		td.sampleCount = 4;
		fallback_ms_texture = wgpuDeviceCreateTexture(device, &td);
		if (fallback_ms_texture) {
			WGPUTextureViewDescriptor vd = {};
			vd.format = WGPUTextureFormat_R32Float;
			vd.dimension = WGPUTextureViewDimension_2D;
			vd.mipLevelCount = 1;
			vd.arrayLayerCount = 1;
			vd.aspect = WGPUTextureAspect_All;
			fallback_ms_texture_view = wgpuTextureCreateView(fallback_ms_texture, &vd);
		}
	}

	// Create dummy samplers for BGL rebinding.
	// When a bind group must be re-created with a different BGL (Comparison↔Filtering),
	// these dummy samplers are substituted for the mismatched entries.
	{
		WGPUSamplerDescriptor sd = {};
		sd.addressModeU = WGPUAddressMode_ClampToEdge;
		sd.addressModeV = WGPUAddressMode_ClampToEdge;
		sd.addressModeW = WGPUAddressMode_ClampToEdge;
		sd.magFilter = WGPUFilterMode_Linear;
		sd.minFilter = WGPUFilterMode_Linear;
		sd.mipmapFilter = WGPUMipmapFilterMode_Linear;
		sd.maxAnisotropy = 1;
		dummy_filtering_sampler = wgpuDeviceCreateSampler(device, &sd);

		WGPUSamplerDescriptor csd = {};
		csd.addressModeU = WGPUAddressMode_ClampToEdge;
		csd.addressModeV = WGPUAddressMode_ClampToEdge;
		csd.addressModeW = WGPUAddressMode_ClampToEdge;
		csd.magFilter = WGPUFilterMode_Linear;
		csd.minFilter = WGPUFilterMode_Linear;
		csd.mipmapFilter = WGPUMipmapFilterMode_Linear;
		csd.maxAnisotropy = 1;
		csd.compare = WGPUCompareFunction_Less;
		dummy_comparison_sampler = wgpuDeviceCreateSampler(device, &csd);

		// Nearest/non-filtering: substituted whenever a shader's own BGL requires
		// NonFiltering (a sampler statically used with a texture_depth_* via a
		// non-comparison sample call — see webgpu_notes/TASKS.md Task 8.9).
		WGPUSamplerDescriptor nfd = {};
		nfd.addressModeU = WGPUAddressMode_ClampToEdge;
		nfd.addressModeV = WGPUAddressMode_ClampToEdge;
		nfd.addressModeW = WGPUAddressMode_ClampToEdge;
		nfd.magFilter = WGPUFilterMode_Nearest;
		nfd.minFilter = WGPUFilterMode_Nearest;
		nfd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
		nfd.maxAnisotropy = 1;
		dummy_nonfiltering_sampler = wgpuDeviceCreateSampler(device, &nfd);
	}

	// Create aliasing stub buffer — substituted for duplicate writable storage buffer bindings
	// in the same uniform set. WebGPU forbids two writable storage bindings that alias the same
	// underlying buffer (Vulkan allows it via barriers). When Godot passes the same buffer for
	// both SourceEmission and DestEmission in the particle compute shader, we redirect the
	// second occurrence to this dummy buffer so the bind group passes validation.
	{
		WGPUBufferDescriptor stub_desc = {};
		stub_desc.size = ALIASING_STUB_BUFFER_SIZE;
		stub_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
		aliasing_stub_buffer = wgpuDeviceCreateBuffer(device, &stub_desc);
	}

	// Create shader container format.
	shader_container_format = memnew(RenderingShaderContainerFormatWebGPU);

	// NOTE: 'uncapturederror' and device-lost listeners are registered in JS
	// (platform/web/js/engine/engine.js, right after requestDevice() resolves)
	// rather than here — this used to also register a second, redundant pair
	// of listeners on the C++ side, double-logging every real error. See
	// plan-of-attack.md Tier 1 #5. The WEBGPU_VERBOSE diagnostic block below
	// still has its own uncapturederror listener, but it's opt-in and off by
	// default, so it doesn't conflict with the always-on JS one in practice.

	// Install main-thread JS diagnostic patches early — as soon as the device is
	// ready, before any pipelines are created. This ensures we intercept EVERY
	// createRenderPipeline/createShaderModule/createBindGroupLayout call. We also
	// kick off a parallel createRenderPipelineAsync() to catch validation errors
	// that the error-scope path misses (Dawn may defer validation past popErrorScope).
	//
	// Gated behind WEBGPU_VERBOSE because the parallel async pipeline creation
	// doubles every render-pipeline compile, and the per-module getCompilationInfo()
	// forces a sync compile path. On shiny_gen with ~383 shaders, leaving these on
	// adds ~12s to startup. Re-enable by uncommenting #define WEBGPU_VERBOSE above.
#ifdef WEBGPU_VERBOSE
	MAIN_THREAD_EM_ASM({
		var d = Module['preinitializedWebGPUDevice'];
		if (!d) {
			console.error('[DIAG-PATCH] preinitializedWebGPUDevice missing');
			return;
		}

		// Always-on uncaptured error logger.
		if (!d._uncapturedPatched) {
			d.addEventListener('uncapturederror', function(e) {
				console.error('[Godot] WebGPU uncaptured error: ' + e.error.constructor.name);
				console.error(e.error.message); });
			d._uncapturedPatched = true;
		}

		if (!d._pipelinePatched) {
			var origCreate = d.createRenderPipeline.bind(d);
			var origCreateAsync = d.createRenderPipelineAsync.bind(d);
			var pipeCount = 0;
			d.createRenderPipeline = function(desc) {
				var myId = pipeCount++;
				var label = (desc && desc.label) || '(unlabeled)';
				// Error-scope path (may miss deferred-validation errors).
				d.pushErrorScope('validation');
				var pipeline = origCreate(desc);
				d.popErrorScope().then(function(err) {
					if (err) {
						console.error('[JS-PCREATE-FAIL#' + myId + '] label="' + label + '" | ' + err.message.substring(0, 2000));
					}
				});
				// Parallel async path (authoritative — catches all validation errors).
				origCreateAsync(desc).then(function(_p){
												   // Success — no-op.
										   },
						function(err) {
					var msg = (err && err.message) ? err.message : String(err);
					console.error('[JS-PCREATE-ASYNC-FAIL#' + myId + '] label="' + label + '" | ' + msg.substring(0, 2000)); });
				// Log every ENTER so we can correlate JS IDs with Godot pipe#N.
				console.log('[JS-PCREATE-ENTER#' + myId + '] label="' + label + '"');
				return pipeline;
			}
			d._pipelinePatched = true;
			console.log('[DIAG-PATCH] createRenderPipeline patched on main thread (initialize)');
		}

		if (!d._shaderModPatched) {
			var origCreateMod = d.createShaderModule.bind(d);
			var modCount = 0;
			d.createShaderModule = function(desc) {
				var myId = modCount++;
				var label = (desc && desc.label) || '(unlabeled)';
				d.pushErrorScope('validation');
				var mod = origCreateMod(desc);
				d.popErrorScope().then(function(err) {
					if (err) {
						console.error('[JS-SMCREATE-FAIL#' + myId + '] label="' + label + '" | ' + err.message.substring(0, 2000));
					}
				});
				if (mod && mod.getCompilationInfo) {
					mod.getCompilationInfo().then(function(info) {
						if (!info || !info.messages || info.messages.length == = 0) {
							return;
						}
						for (var i = 0; i < info.messages.length; i++) {
							var m = info.messages[i];
							if (m.type == = 'error') {
								console.error('[JS-SMCOMPILE-ERR#' + myId + '] label="' + label + '" line=' + m.lineNum + ':' + m.linePos + ' | ' + m.message.substring(0, 1200));
							}
						}
					});
				}
				return mod;
			}
			d._shaderModPatched = true;
			console.log('[DIAG-PATCH] createShaderModule patched on main thread (initialize)');
		}

		if (!d._bglPatched) {
			var origBGL = d.createBindGroupLayout.bind(d);
			d.createBindGroupLayout = function(desc) {
				var label = (desc && desc.label) || '(unlabeled)';
				d.pushErrorScope('validation');
				var layout = origBGL(desc);
				d.popErrorScope().then(function(err) {
					if (err) {
						console.error('[JS-BGL-FAIL] label="' + label + '" | ' + err.message.substring(0, 2000));
					}
				});
				return layout;
			}
			d._bglPatched = true;
		}

		if (!d._plytPatched) {
			var origPLYT = d.createPipelineLayout.bind(d);
			d.createPipelineLayout = function(desc) {
				var label = (desc && desc.label) || '(unlabeled)';
				d.pushErrorScope('validation');
				var layout = origPLYT(desc);
				d.popErrorScope().then(function(err) {
					if (err) {
						console.error('[JS-PLYT-FAIL] label="' + label + '" | ' + err.message.substring(0, 2000));
					}
				});
				return layout;
			}
			d._plytPatched = true;
		}
	});
#endif // WEBGPU_VERBOSE

	return OK;
}

void RenderingDeviceDriverWebGPU::_check_capabilities() {
	capabilities.device_family = DEVICE_UNKNOWN;
	capabilities.version_major = 1;
	capabilities.version_minor = 0;

	// Query actual device limits.
	device_limits = WGPU_LIMITS_INIT;
	WGPUStatus status = wgpuDeviceGetLimits(device, &device_limits);
	if (status != WGPUStatus_Success) {
		WARN_PRINT("WebGPU: Failed to query device limits, using spec minimums.");
	}

	// Check for timestamp query support.
	// NOTE: Timestamp query readback is disabled for now. The async mapAsync
	// on the readback buffer can get permanently stuck in "mapping pending" state
	// when frames are slow (emdawnwebgpu does not reliably cancel pending maps via
	// wgpuBufferUnmap). This causes "buffer used in submit while mapped" validation
	// errors that corrupt rendering. GPU timestamp profiling is sacrificed — the
	// profiler reports "gpu=N/A" — but rendering correctness is preserved.
	// TODO: Re-enable once emdawnwebgpu properly implements unmap-cancels-pending-map.
	timestamp_supported = false;
	if (wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery)) {
		print_verbose("WebGPU: Timestamp query feature is available (readback disabled due to buffer mapping issue).");
	}

	// float32-filterable: required for linear sampling of R32Float / RG32Float / RGBA32Float.
	// Forward Mobile's HDR post-processing path samples 32F render targets with linear
	// samplers, so without this feature those samplers must fall back to NEAREST.
	// Feature name enum value 13 per WebGPU spec (not yet in the emdawnwebgpu 4.0.10 header enum).
	float32_filterable_supported = wgpuDeviceHasFeature(device, (WGPUFeatureName)13);
	if (float32_filterable_supported) {
		print_verbose("WebGPU: float32-filterable feature is available.");
	} else {
		WARN_PRINT("WebGPU: float32-filterable feature NOT available — 32F linear sampling will fall back to nearest.");
	}

	// float32-blendable: required for blending on R32Float / RG32Float / RGBA32Float
	// render targets. Without this, blend operations on float32 targets silently fail
	// (particles, post-processing compositing).
	// Feature name enum value 14 per WebGPU spec (0x0E, not yet in emdawnwebgpu 4.0.10 header).
	float32_blendable_supported = wgpuDeviceHasFeature(device, (WGPUFeatureName)14);
	if (float32_blendable_supported) {
		print_verbose("WebGPU: float32-blendable feature is available.");
	} else {
		WARN_PRINT("WebGPU: float32-blendable feature NOT available — blend on float32 targets will be disabled.");
	}

	// texture-formats-tier1: adds storage binding support for r8unorm, rg8unorm, etc.
	// The emdawnwebgpu 4.0.10 header lacks the WGPUFeatureName enum value for this
	// feature, so query the JS device object directly.
	has_texture_formats_tier1 = (bool)EM_ASM_INT({
		var d = Module['preinitializedWebGPUDevice'];
		return (d && d.features && d.features.has('texture-formats-tier1')) ? 1 : 0;
	});
	if (has_texture_formats_tier1) {
		print_verbose("WebGPU: texture-formats-tier1 feature is available — r8/rg8 storage formats supported natively.");
	}

	// readonly-and-readwrite-storage-textures: allows read and read_write access
	// modes on storage textures. Without this, only write-only is valid.
	has_rw_storage_textures = (bool)EM_ASM_INT({
		var d = Module['preinitializedWebGPUDevice'];
		return (d && d.features && d.features.has('readonly-and-readwrite-storage-textures')) ? 1 : 0;
	});
	if (has_rw_storage_textures) {
		print_verbose("WebGPU: readonly-and-readwrite-storage-textures feature is available.");
	} else {
		print_verbose("WebGPU: readonly-and-readwrite-storage-textures NOT available — will split read_write storage textures.");
	}

	// Optional texture-compression families. The JS shell opts in to these when
	// the adapter supports them; we must match that here so Godot only advertises
	// the corresponding DataFormats when they'll actually work.
	has_texture_compression_bc = wgpuDeviceHasFeature(device, WGPUFeatureName_TextureCompressionBC);
	has_texture_compression_etc2 = wgpuDeviceHasFeature(device, WGPUFeatureName_TextureCompressionETC2);
	has_texture_compression_astc = wgpuDeviceHasFeature(device, WGPUFeatureName_TextureCompressionASTC);
	if (has_texture_compression_bc) {
		print_verbose("WebGPU: texture-compression-bc feature is available.");
	}
	if (has_texture_compression_etc2) {
		print_verbose("WebGPU: texture-compression-etc2 feature is available.");
	}
	if (has_texture_compression_astc) {
		print_verbose("WebGPU: texture-compression-astc feature is available.");
	}

	// Multiview not supported in WebGPU.
	multiview_capabilities.is_supported = false;

	// Fragment shading rate not supported.
	fsr_capabilities = {};

	// Fragment density map not supported.
	fdm_capabilities = {};
}

// =============================================================================
// BUFFERS
// =============================================================================

RDD::BufferID RenderingDeviceDriverWebGPU::buffer_create(uint64_t p_size, BitField<BufferUsageBits> p_usage, MemoryAllocationType p_allocation_type, uint64_t p_frames_drawn) {
	WGBuffer *buf = new WGBuffer();

	// WebGPU buffer sizes must be a multiple of 4.
	uint64_t aligned_size = (p_size + 3) & ~3ULL;

	// Task 7.5: BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT means Godot wants to rotate
	// through `frame_count` slices of this buffer. Each slice must be aligned to
	// the device's minUniformBufferOffsetAlignment (typically 256) because the
	// slice offset is passed as a dynamic offset to wgpuRenderPassEncoderSetBindGroup,
	// and dynamic offsets must be multiples of that alignment. Without this
	// alignment pass, the original Phase 7 commit (8d48436801) would fail validation
	// whenever p_size was not already a 256-multiple.
	const bool is_dynamic_persistent = p_usage.has_flag(BUFFER_USAGE_DYNAMIC_PERSISTENT_BIT);
	if (is_dynamic_persistent && frame_count > 1) {
		uint32_t alignment = device_limits.minUniformBufferOffsetAlignment;
		if (alignment == 0 || alignment == WGPU_LIMIT_U32_UNDEFINED) {
			alignment = 256; // WebGPU spec default minimum.
		}
		aligned_size = (aligned_size + alignment - 1) & ~(uint64_t)(alignment - 1);
		buf->per_frame_size = aligned_size;
		aligned_size *= frame_count;
		buf->frame_idx = 0; // Mark as dynamic — first slice is index 0.
	}

	buf->usage = _buffer_usage_to_wgpu(p_usage);
	buf->usage |= WGPUBufferUsage_CopyDst; // Always allow writes.
	buf->size = aligned_size;

	// Don't add MapRead to generic CPU buffers — it conflicts with CopySrc.
	// Compute readback uses buffer_get_data_direct() which creates its own
	// staging buffer with the correct CopyDst|MapRead usage.
	//
	// Task 7.8: Narrow exception — a "CPU + TRANSFER_TO + no TRANSFER_FROM"
	// buffer is a pure GPU→CPU staging buffer (dest-only, never the source of
	// another copy). For these, WebGPU spec permits MapRead (since CopySrc is
	// not present, there's no CopySrc↔MapRead conflict), and the existing
	// buffer_map() async-readback codepath needs MapRead to be valid.
	const bool wants_transfer_to = p_usage.has_flag(BUFFER_USAGE_TRANSFER_TO_BIT);
	const bool wants_transfer_from = p_usage.has_flag(BUFFER_USAGE_TRANSFER_FROM_BIT);
	if (p_allocation_type == MEMORY_ALLOCATION_TYPE_CPU && wants_transfer_to && !wants_transfer_from) {
		buf->is_readback = true;
		buf->usage |= WGPUBufferUsage_MapRead;
		// CopySrc must NOT be set for MapRead to be legal; _buffer_usage_to_wgpu
		// derives CopySrc from TRANSFER_FROM_BIT, which we've just confirmed is off.
	}

	WGPUBufferDescriptor desc = {};
	desc.size = aligned_size;
	desc.usage = buf->usage;
	desc.mappedAtCreation = false;

	buf->handle = wgpuDeviceCreateBuffer(device, &desc);
	if (buf->handle == nullptr) {
		delete buf;
		ERR_FAIL_V(BufferID());
	}

	return BufferID(buf);
}

RDD::BufferID RenderingDeviceDriverWebGPU::buffer_create_with_data(uint64_t p_size, BitField<BufferUsageBits> p_usage, MemoryAllocationType p_allocation_type, const uint8_t *p_data, uint64_t p_data_size) {
	WGBuffer *buf = new WGBuffer();

	uint64_t aligned_size = (p_size + 3) & ~3ULL;

	buf->usage = _buffer_usage_to_wgpu(p_usage);
	buf->usage |= WGPUBufferUsage_CopyDst; // Always allow writes.
	buf->size = aligned_size;

	WGPUBufferDescriptor desc = {};
	desc.size = aligned_size;
	desc.usage = buf->usage;
	desc.mappedAtCreation = true;

	buf->handle = wgpuDeviceCreateBuffer(device, &desc);
	if (buf->handle == nullptr) {
		delete buf;
		ERR_FAIL_V(BufferID());
	}

	// Write initial data directly into the mapped range — no staging buffer,
	// no wgpuQueueWriteBuffer, no command encoder copy needed.
	void *mapped = wgpuBufferGetMappedRange(buf->handle, 0, aligned_size);
	if (mapped) {
		memcpy(mapped, p_data, p_data_size);
		// Zero-fill any padding between data end and aligned buffer size.
		if (aligned_size > p_data_size) {
			memset((uint8_t *)mapped + p_data_size, 0, aligned_size - p_data_size);
		}
	}
	wgpuBufferUnmap(buf->handle);

	return BufferID(buf);
}

bool RenderingDeviceDriverWebGPU::buffer_set_texel_format(BufferID p_buffer, DataFormat p_format) {
	// WebGPU has no texel buffer views. Stub: store format, emulate later if needed.
	return true;
}

void RenderingDeviceDriverWebGPU::buffer_free(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(buf);

	// Drop any readback-cache entry for this buffer.
	uint64_t key = (uint64_t)(uintptr_t)buf;
	if (_readback_cache.has(key)) {
		ReadbackEntry *entry = _readback_cache[key];
		if (!entry->map_complete) {
			entry->cancelled = true;
		} else {
			if (entry->staging) {
				wgpuBufferRelease(entry->staging);
			}
			if (entry->shadow) {
				memfree(entry->shadow);
			}
			memdelete(entry);
		}
		_readback_cache.erase(key);
	}

	// If an async map callback is in flight for this buffer, mark it freed
	// and let the callback handle cleanup (use-after-free prevention).
	if (buf->map_pending) {
		buf->freed = true;
		return;
	}

	if (buf->handle) {
		wgpuBufferRelease(buf->handle);
	}
	if (buf->shadow_map) {
		memfree(buf->shadow_map);
	}
	delete buf;
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_allocation_size(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, 0);
	return buf->size;
}

// Callback for deferred buffer map readback (same signature as _timestamp_readback_callback).
// Copies GPU buffer data into the WGBuffer shadow_map when the async map resolves.
static void _buffer_deferred_map_cb(WGPUMapAsyncStatus p_status, WGPUStringView p_message, void *p_userdata1, void *p_userdata2) {
	WGBuffer *buf = (WGBuffer *)p_userdata1;
	if (!buf) {
		return;
	}

	buf->map_pending = false;

	// Buffer was freed while this async map was in flight.
	if (buf->freed) {
		if (buf->handle) {
			if (p_status == WGPUMapAsyncStatus_Success) {
				wgpuBufferUnmap(buf->handle);
			}
			wgpuBufferRelease(buf->handle);
		}
		if (buf->shadow_map) {
			memfree(buf->shadow_map);
		}
		delete buf;
		return;
	}

	if (p_status == WGPUMapAsyncStatus_Success) {
		const void *mapped = wgpuBufferGetConstMappedRange(buf->handle, 0, buf->size);
		if (mapped && buf->shadow_map) {
			memcpy(buf->shadow_map, mapped, buf->size);
		}
		wgpuBufferUnmap(buf->handle);
	} else {
		ERR_PRINT(vformat("WebGPU _buffer_deferred_map_cb: FAILED status=%d", (int)p_status));
	}
	buf->map_complete = true;
}

uint8_t *RenderingDeviceDriverWebGPU::buffer_map(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, nullptr);

	// For readback buffers (staging buffers used by buffer_get_data), perform
	// async map to get actual GPU data.  This is critical for compute shader
	// readback, screenshot capture, and any CPU-side read of GPU results.
	//
	// The pattern: wgpuBufferMapAsync with AllowSpontaneous callback mode,
	// then wgpuQueueSubmit (empty) to flush, followed by polling via
	// wgpuBufferGetMapState until the map completes.
	if (buf->is_readback && buf->handle) {
		// WebGPU async readback: the buffer may have been mapped by
		// buffer_initiate_async_map() (called after GPU submit). We try
		// ProcessEvents to deliver any pending callbacks, and also check the
		// map state directly — if the buffer is Mapped, we can read it now
		// even if the C callback hasn't fired yet.
		if (!buf->shadow_map) {
			buf->shadow_map = (uint8_t *)memalloc(buf->size);
			memset(buf->shadow_map, 0, buf->size);
		}

		// Try to deliver pending callbacks.
		WGPUInstance inst = context_driver ? context_driver->get_instance() : nullptr;
		if (inst) {
			wgpuInstanceProcessEvents(inst);
		}

		// If the callback already fired, shadow has data.
		if (buf->map_complete) {
			buf->map_complete = false;
			return buf->shadow_map;
		}

		// Callback didn't fire, but check if the buffer is mapped anyway.
		// emdawnwebgpu may have completed the map at the JS level even
		// though the C callback hasn't been delivered yet.
		WGPUBufferMapState state = wgpuBufferGetMapState(buf->handle);
		if (state == WGPUBufferMapState_Mapped) {
			const void *mapped = wgpuBufferGetConstMappedRange(buf->handle, 0, buf->size);
			if (mapped) {
				memcpy(buf->shadow_map, mapped, buf->size);
				// Buffer was mapped at JS level before C callback fired.
			}
			wgpuBufferUnmap(buf->handle);
			return buf->shadow_map;
		}

		return buf->shadow_map;
	}

	// For non-readback buffers, use the shadow CPU buffer (for upload staging).
	// Mark dirty so buffer_unmap() will flush to GPU.
	if (!buf->shadow_map) {
		buf->shadow_map = (uint8_t *)memalloc(buf->size);
		memset(buf->shadow_map, 0, buf->size);
	}
	buf->map_dirty = true;
	return buf->shadow_map;
}

void RenderingDeviceDriverWebGPU::buffer_unmap(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(buf);
	if (buf->shadow_map && buf->map_dirty) {
		// Flush only the dirty range if one was set, otherwise fall back to the
		// full buffer. command_copy_buffer_to_texture and command_copy_buffer
		// clear map_dirty after handling the transfer themselves, so in practice
		// this path is only hit for persistent dynamic buffers or unknown callers.
		uint64_t flush_offset = 0;
		uint64_t flush_size = buf->size;
		if (buf->dirty_end > buf->dirty_offset) {
			flush_offset = buf->dirty_offset;
			flush_size = buf->dirty_end - buf->dirty_offset;
		}
		wgpuQueueWriteBuffer(queue, buf->handle, flush_offset, buf->shadow_map + flush_offset, flush_size);
		buf->map_dirty = false;
		buf->dirty_offset = 0;
		buf->dirty_end = 0;
	}
}

uint8_t *RenderingDeviceDriverWebGPU::buffer_persistent_map_advance(BufferID p_buffer, uint64_t p_frames_drawn) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, nullptr);
	if (!buf->shadow_map) {
		buf->shadow_map = (uint8_t *)memalloc(buf->size);
		memset(buf->shadow_map, 0, buf->size);
	}
	// Task 7.5: For dynamic persistent buffers, rotate to the next slice and
	// return a pointer into that slice. Godot writes one frame's worth of data
	// here, and the GPU reads from `frame_idx * per_frame_size` via dynamic offset.
	if (buf->is_dynamic() && buf->per_frame_size > 0 && frame_count > 1) {
		buf->frame_idx = (buf->frame_idx + 1) % frame_count;
		uint64_t slice_offset = (uint64_t)buf->frame_idx * buf->per_frame_size;
		// Set dirty range to just this frame's slice so buffer_flush() only
		// writes per_frame_size bytes instead of the entire multi-frame buffer.
		buf->dirty_offset = slice_offset;
		buf->dirty_end = slice_offset + buf->per_frame_size;
		return buf->shadow_map + slice_offset;
	}
	return buf->shadow_map;
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_dynamic_offsets(Span<BufferID> p_buffers) {
	// Task 7.5: Standalone path — packs frame_idx for each dynamic buffer into
	// 2-bit shifted slots (matching the Vulkan pattern). Consumed by the render
	// graph for buffers passed to command_render_bind_vertex_buffers (which
	// currently doesn't honor dynamic offsets on WebGPU; harmless to return here).
	uint64_t mask = 0;
	uint64_t shift = 0;
	for (const BufferID &bid : p_buffers) {
		const WGBuffer *buf = (const WGBuffer *)bid.id;
		if (!buf || !buf->is_dynamic()) {
			continue;
		}
		mask |= (uint64_t)buf->frame_idx << shift;
		shift += 2; // Matches Vulkan: frame_count never exceeds 4.
	}
	return mask;
}

void RenderingDeviceDriverWebGPU::buffer_flush(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	if (buf && buf->shadow_map) {
		// Flush only the dirty range if one was set (e.g., by
		// buffer_persistent_map_advance), otherwise fall back to full buffer.
		uint64_t flush_offset = 0;
		uint64_t flush_size = buf->size;
		if (buf->dirty_end > buf->dirty_offset) {
			flush_offset = buf->dirty_offset;
			flush_size = buf->dirty_end - buf->dirty_offset;
		}
		wgpuQueueWriteBuffer(queue, buf->handle, flush_offset, buf->shadow_map + flush_offset, flush_size);
		buf->dirty_offset = 0;
		buf->dirty_end = 0;
	}
}

void RenderingDeviceDriverWebGPU::buffer_write_direct(BufferID p_buffer, uint64_t p_offset, uint64_t p_size, const void *p_data) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(buf);
	uint64_t aligned_size = (p_size + 3) & ~3ULL;
	wgpuQueueWriteBuffer(queue, buf->handle, p_offset, p_data, aligned_size);
}

uint64_t RenderingDeviceDriverWebGPU::buffer_get_device_address(BufferID p_buffer) {
	return 0; // No device addresses in WebGPU.
}

void RenderingDeviceDriverWebGPU::buffer_initiate_async_map(BufferID p_buffer) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	if (!buf || !buf->is_readback || !buf->handle) {
		WARN_PRINT("WebGPU buffer_initiate_async_map: skipped (null/not-readback)");
		return;
	}

	// Consume any completed previous map so shadow_map has its data,
	// then start a fresh map for the data the GPU just wrote.
	if (buf->map_complete) {
		buf->map_complete = false;
	}

	// Only initiate if the buffer isn't already pending a map.
	WGPUBufferMapState state = wgpuBufferGetMapState(buf->handle);
	if (state == WGPUBufferMapState_Unmapped) {
		if (!buf->shadow_map) {
			buf->shadow_map = (uint8_t *)memalloc(buf->size);
			memset(buf->shadow_map, 0, buf->size);
		}
		buf->map_pending = true;
		WGPUBufferMapCallbackInfo cb = {};
		cb.mode = WGPUCallbackMode_AllowSpontaneous;
		cb.callback = _buffer_deferred_map_cb;
		cb.userdata1 = buf;
		wgpuBufferMapAsync(buf->handle, WGPUMapMode_Read, 0, buf->size, cb);
	} else {
		// Buffer already pending or mapped, skip duplicate mapAsync.
	}
}

uint32_t RenderingDeviceDriverWebGPU::texture_get_gpu_pixel_size(TextureID p_texture) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	if (!tex || tex->rd_format == DATA_FORMAT_MAX) {
		return 0;
	}
	uint32_t gpu_size = wgpu_format_pixel_size(tex->format);
	uint32_t rd_size = get_image_format_pixel_size(tex->rd_format);
	return (gpu_size != rd_size) ? gpu_size : 0;
}

void RenderingDeviceDriverWebGPU::texture_readback_convert(TextureID p_texture,
		const uint8_t *p_src, uint32_t p_src_pitch,
		uint8_t *p_dst, uint32_t p_dst_pitch,
		uint32_t p_width, uint32_t p_height) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	if (!tex) {
		return;
	}
	uint32_t gpu_size = wgpu_format_pixel_size(tex->format);
	uint32_t rd_size = get_image_format_pixel_size(tex->rd_format);
	if (gpu_size == rd_size) {
		// No conversion needed — straight copy.
		for (uint32_t y = 0; y < p_height; y++) {
			memcpy(p_dst + y * p_dst_pitch, p_src + y * p_src_pitch, p_width * rd_size);
		}
		return;
	}

	// Float16 → Float32 readback: reverse of the float32→float16 downgrade.
	bool is_f16_to_f32 = false;
	uint32_t f16_channels = 0;
	if (tex->format == WGPUTextureFormat_R16Float &&
			tex->rd_format == DATA_FORMAT_R32_SFLOAT) {
		is_f16_to_f32 = true;
		f16_channels = 1;
	} else if (tex->format == WGPUTextureFormat_RG16Float &&
			tex->rd_format == DATA_FORMAT_R32G32_SFLOAT) {
		is_f16_to_f32 = true;
		f16_channels = 2;
	} else if (tex->format == WGPUTextureFormat_RGBA16Float &&
			tex->rd_format == DATA_FORMAT_R32G32B32A32_SFLOAT) {
		is_f16_to_f32 = true;
		f16_channels = 4;
	}

	if (is_f16_to_f32) {
		for (uint32_t y = 0; y < p_height; y++) {
			const uint16_t *src_row = (const uint16_t *)(p_src + y * p_src_pitch);
			float *dst_row = (float *)(p_dst + y * p_dst_pitch);
			for (uint32_t x = 0; x < p_width; x++) {
				for (uint32_t c = 0; c < f16_channels; c++) {
					dst_row[x * f16_channels + c] = Math::half_to_float(src_row[x * f16_channels + c]);
				}
			}
		}
		return;
	}

	// R8/RG8 promoted to R32Float/RG32Float: convert float [0,1] → uint8 [0,255]
	// R8/RG8 promoted to R32Uint/RG32Uint: convert uint32 → uint8
	uint32_t channels = rd_size; // For R8 = 1 channel, RG8 = 2 channels, etc.
	bool is_float = (tex->format == WGPUTextureFormat_R32Float ||
			tex->format == WGPUTextureFormat_RG32Float);
	bool is_uint = (tex->format == WGPUTextureFormat_R32Uint ||
			tex->format == WGPUTextureFormat_RG32Uint);
	bool is_sint = (tex->format == WGPUTextureFormat_R32Sint ||
			tex->format == WGPUTextureFormat_RG32Sint);

	for (uint32_t y = 0; y < p_height; y++) {
		const uint8_t *src_row = p_src + y * p_src_pitch;
		uint8_t *dst_row = p_dst + y * p_dst_pitch;
		for (uint32_t x = 0; x < p_width; x++) {
			for (uint32_t c = 0; c < channels; c++) {
				uint32_t src_offset = (x * channels + c) * 4; // 4 bytes per component (32-bit)
				uint32_t dst_offset = x * channels + c;
				if (is_float) {
					float f;
					memcpy(&f, src_row + src_offset, 4);
					f = CLAMP(f, 0.0f, 1.0f);
					dst_row[dst_offset] = (uint8_t)(f * 255.0f + 0.5f);
				} else if (is_uint) {
					uint32_t u;
					memcpy(&u, src_row + src_offset, 4);
					dst_row[dst_offset] = (uint8_t)MIN(u, 255u);
				} else if (is_sint) {
					int32_t s;
					memcpy(&s, src_row + src_offset, 4);
					dst_row[dst_offset] = (uint8_t)CLAMP(s, 0, 127);
				}
			}
		}
	}
}

void RenderingDeviceDriverWebGPU::texture_upload_convert(TextureID p_texture,
		const uint8_t *p_src, uint32_t p_src_pitch,
		uint8_t *p_dst, uint32_t p_dst_pitch,
		uint32_t p_width, uint32_t p_height) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	if (!tex) {
		return;
	}
	uint32_t gpu_size = wgpu_format_pixel_size(tex->format);
	uint32_t rd_size = get_image_format_pixel_size(tex->rd_format);
	if (gpu_size == rd_size) {
		for (uint32_t y = 0; y < p_height; y++) {
			memcpy(p_dst + y * p_dst_pitch, p_src + y * p_src_pitch, p_width * rd_size);
		}
		return;
	}

	// Float32 → Float16 downgrade: convert f32 data to f16 for devices
	// lacking float32-filterable (e.g. CurveTextures on Adreno).
	bool is_f32_to_f16 = false;
	uint32_t f16_channels = 0;
	if (tex->format == WGPUTextureFormat_R16Float &&
			tex->rd_format == DATA_FORMAT_R32_SFLOAT) {
		is_f32_to_f16 = true;
		f16_channels = 1;
	} else if (tex->format == WGPUTextureFormat_RG16Float &&
			tex->rd_format == DATA_FORMAT_R32G32_SFLOAT) {
		is_f32_to_f16 = true;
		f16_channels = 2;
	} else if (tex->format == WGPUTextureFormat_RGBA16Float &&
			tex->rd_format == DATA_FORMAT_R32G32B32A32_SFLOAT) {
		is_f32_to_f16 = true;
		f16_channels = 4;
	}

	if (is_f32_to_f16) {
		WEBGPU_DIAG({ console.log('[F32-UPLOAD] f32→f16 ch=' + $0 + ' w=' + $1 + ' h=' + $2 + ' src_pitch=' + $3 + ' dst_pitch=' + $4 + ' gpu_fmt=' + $5 + ' rd_fmt=' + $6); }, (int)f16_channels, (int)p_width, (int)p_height, (int)p_src_pitch, (int)p_dst_pitch, (int)tex->format, (int)tex->rd_format);
		for (uint32_t y = 0; y < p_height; y++) {
			const float *src_row = (const float *)(p_src + y * p_src_pitch);
			uint16_t *dst_row = (uint16_t *)(p_dst + y * p_dst_pitch);
			for (uint32_t x = 0; x < p_width; x++) {
				for (uint32_t c = 0; c < f16_channels; c++) {
					dst_row[x * f16_channels + c] = Math::make_half_float(src_row[x * f16_channels + c]);
				}
			}
		}
		return;
	}

	// Log if we reach here unexpectedly for a format-downgraded texture.
	if (tex->format == WGPUTextureFormat_R16Float || tex->format == WGPUTextureFormat_RG16Float || tex->format == WGPUTextureFormat_RGBA16Float) {
		WEBGPU_DIAG({ console.error('[F32-UPLOAD-MISS] NO conversion for gpu_fmt=' + $0 + ' rd_fmt=' + $1 + ' gpu_size=' + $2 + ' rd_size=' + $3); }, (int)tex->format, (int)tex->rd_format, (int)gpu_size, (int)rd_size);
	}

	// R8/RG8 promoted to R32Float/RG32Float: convert uint8 [0,255] → float [0,1]
	uint32_t channels = rd_size;
	bool is_float = (tex->format == WGPUTextureFormat_R32Float ||
			tex->format == WGPUTextureFormat_RG32Float);
	bool is_uint = (tex->format == WGPUTextureFormat_R32Uint ||
			tex->format == WGPUTextureFormat_RG32Uint);
	bool is_sint = (tex->format == WGPUTextureFormat_R32Sint ||
			tex->format == WGPUTextureFormat_RG32Sint);

	for (uint32_t y = 0; y < p_height; y++) {
		const uint8_t *src_row = p_src + y * p_src_pitch;
		uint8_t *dst_row = p_dst + y * p_dst_pitch;
		for (uint32_t x = 0; x < p_width; x++) {
			for (uint32_t c = 0; c < channels; c++) {
				uint32_t src_offset = x * channels + c;
				uint32_t dst_offset = (x * channels + c) * 4;
				if (is_float) {
					float f = (float)src_row[src_offset] / 255.0f;
					memcpy(dst_row + dst_offset, &f, 4);
				} else if (is_uint) {
					uint32_t u = src_row[src_offset];
					memcpy(dst_row + dst_offset, &u, 4);
				} else if (is_sint) {
					int32_t s = (int32_t)src_row[src_offset];
					memcpy(dst_row + dst_offset, &s, 4);
				}
			}
		}
	}
}

// =============================================================================
// ASYNC BUFFER READBACK (WebGPU-specific)
// =============================================================================

void RenderingDeviceDriverWebGPU::_readback_map_cb(WGPUMapAsyncStatus p_status, WGPUStringView p_message, void *p_userdata1, void *p_userdata2) {
	ReadbackEntry *entry = (ReadbackEntry *)p_userdata1;
	if (!entry) {
		return;
	}

	// Source was freed while the async map was in flight. Clean up the
	// orphaned entry that was kept alive specifically for this callback.
	if (entry->cancelled) {
		if (entry->staging) {
			if (p_status == WGPUMapAsyncStatus_Success) {
				wgpuBufferUnmap(entry->staging);
			}
			wgpuBufferRelease(entry->staging);
		}
		if (entry->shadow) {
			memfree(entry->shadow);
		}
		memdelete(entry);
		return;
	}

	if (!entry->staging) {
		return;
	}

	if (p_status == WGPUMapAsyncStatus_Success) {
		const void *mapped = wgpuBufferGetConstMappedRange(entry->staging, 0, entry->size);
		if (mapped && entry->shadow) {
			memcpy(entry->shadow, mapped, entry->size);
			entry->has_data = true;
		}
		wgpuBufferUnmap(entry->staging);
	}
	entry->map_pending = false;
	entry->map_complete = true;
}

bool RenderingDeviceDriverWebGPU::buffer_get_data_direct(BufferID p_buffer, uint64_t p_offset, uint64_t p_size, Vector<uint8_t> &r_data) {
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL_V(buf, false);

	uint64_t key = (uint64_t)(uintptr_t)buf;
	ReadbackEntry *entry = nullptr;

	if (_readback_cache.has(key)) {
		entry = _readback_cache[key];
	}

	// If we have completed readback data from a previous frame, return it.
	if (entry && entry->has_data && entry->map_complete) {
		r_data.resize(p_size);
		memcpy(r_data.ptrw(), entry->shadow + p_offset, p_size);

		// Initiate a new readback for this frame's data.
		entry->map_complete = false;
		entry->map_pending = true;
		// Copy GPU buffer → persistent staging buffer.
		WGPUCommandEncoderDescriptor enc_desc = {};
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
		wgpuCommandEncoderCopyBufferToBuffer(encoder, buf->handle, 0, entry->staging, 0, entry->size);
		WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
		wgpuQueueSubmit(queue, 1, &cmd);
		wgpuCommandBufferRelease(cmd);
		wgpuCommandEncoderRelease(encoder);

		// Initiate async map for next readback.
		WGPUBufferMapCallbackInfo cb = {};
		cb.mode = WGPUCallbackMode_AllowSpontaneous;
		cb.callback = _readback_map_cb;
		cb.userdata1 = entry;
		wgpuBufferMapAsync(entry->staging, WGPUMapMode_Read, 0, entry->size, cb);

		return true;
	}

	// First call for this buffer, or readback not yet complete.
	// Create persistent staging buffer and initiate first readback.
	if (!entry) {
		entry = memnew(ReadbackEntry);
		entry->size = buf->size;
		entry->shadow = (uint8_t *)memalloc(buf->size);
		memset(entry->shadow, 0, buf->size);

		// Create persistent staging buffer with CopyDst + MapRead.
		WGPUBufferDescriptor desc = {};
		desc.size = (buf->size + 3) & ~3ULL;
		desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
		entry->staging = wgpuDeviceCreateBuffer(device, &desc);
		if (!entry->staging) {
			memfree(entry->shadow);
			memdelete(entry);
			ERR_FAIL_V_MSG(false, "WebGPU: buffer_get_data_direct: failed to create staging buffer.");
		}
		entry->map_complete = false;
		entry->has_data = false;

		_readback_cache[key] = entry;
	}

	if (!entry->map_complete && !entry->map_pending) {
		// Copy GPU buffer → persistent staging buffer.
		entry->map_pending = true;
		WGPUCommandEncoderDescriptor enc_desc = {};
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
		wgpuCommandEncoderCopyBufferToBuffer(encoder, buf->handle, 0, entry->staging, 0, entry->size);
		WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
		wgpuQueueSubmit(queue, 1, &cmd);
		wgpuCommandBufferRelease(cmd);
		wgpuCommandEncoderRelease(encoder);

		// Initiate async map.
		WGPUBufferMapCallbackInfo cb = {};
		cb.mode = WGPUCallbackMode_AllowSpontaneous;
		cb.callback = _readback_map_cb;
		cb.userdata1 = entry;
		wgpuBufferMapAsync(entry->staging, WGPUMapMode_Read, 0, entry->size, cb);
	}

	// Return previous frame's data if available, otherwise signal "not ready"
	// by returning empty data (false → caller sees failure, not misleading zeros).
	if (entry->has_data) {
		r_data.resize(p_size);
		memcpy(r_data.ptrw(), entry->shadow + p_offset, p_size);
		return true;
	} else {
		r_data.clear();
		return false; // Not ready — data will be available next frame.
	}
}

WGPUBufferUsage RenderingDeviceDriverWebGPU::_buffer_usage_to_wgpu(BitField<BufferUsageBits> p_usage) const {
	WGPUBufferUsage flags = 0;
	if (p_usage.has_flag(BUFFER_USAGE_TRANSFER_FROM_BIT)) {
		flags |= WGPUBufferUsage_CopySrc;
	}
	if (p_usage.has_flag(BUFFER_USAGE_TRANSFER_TO_BIT)) {
		flags |= WGPUBufferUsage_CopyDst;
	}
	if (p_usage.has_flag(BUFFER_USAGE_UNIFORM_BIT)) {
		flags |= WGPUBufferUsage_Uniform;
	}
	if (p_usage.has_flag(BUFFER_USAGE_STORAGE_BIT) || p_usage.has_flag(BUFFER_USAGE_TEXEL_BIT)) {
		flags |= WGPUBufferUsage_Storage;
	}
	if (p_usage.has_flag(BUFFER_USAGE_INDEX_BIT)) {
		flags |= WGPUBufferUsage_Index;
	}
	if (p_usage.has_flag(BUFFER_USAGE_VERTEX_BIT)) {
		flags |= WGPUBufferUsage_Vertex;
	}
	if (p_usage.has_flag(BUFFER_USAGE_INDIRECT_BIT)) {
		flags |= WGPUBufferUsage_Indirect;
	}
	return flags;
}

// =============================================================================
// TEXTURES
// =============================================================================

// Returns the sRGB counterpart format for view compatibility, or Undefined if none.
static WGPUTextureFormat _get_srgb_view_format(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_RGBA8Unorm:
			return WGPUTextureFormat_RGBA8UnormSrgb;
		case WGPUTextureFormat_RGBA8UnormSrgb:
			return WGPUTextureFormat_RGBA8Unorm;
		case WGPUTextureFormat_BGRA8Unorm:
			return WGPUTextureFormat_BGRA8UnormSrgb;
		case WGPUTextureFormat_BGRA8UnormSrgb:
			return WGPUTextureFormat_BGRA8Unorm;
		case WGPUTextureFormat_BC1RGBAUnorm:
			return WGPUTextureFormat_BC1RGBAUnormSrgb;
		case WGPUTextureFormat_BC1RGBAUnormSrgb:
			return WGPUTextureFormat_BC1RGBAUnorm;
		case WGPUTextureFormat_BC2RGBAUnorm:
			return WGPUTextureFormat_BC2RGBAUnormSrgb;
		case WGPUTextureFormat_BC2RGBAUnormSrgb:
			return WGPUTextureFormat_BC2RGBAUnorm;
		case WGPUTextureFormat_BC3RGBAUnorm:
			return WGPUTextureFormat_BC3RGBAUnormSrgb;
		case WGPUTextureFormat_BC3RGBAUnormSrgb:
			return WGPUTextureFormat_BC3RGBAUnorm;
		case WGPUTextureFormat_ETC2RGB8Unorm:
			return WGPUTextureFormat_ETC2RGB8UnormSrgb;
		case WGPUTextureFormat_ETC2RGB8UnormSrgb:
			return WGPUTextureFormat_ETC2RGB8Unorm;
		case WGPUTextureFormat_ETC2RGB8A1Unorm:
			return WGPUTextureFormat_ETC2RGB8A1UnormSrgb;
		case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
			return WGPUTextureFormat_ETC2RGB8A1Unorm;
		case WGPUTextureFormat_ETC2RGBA8Unorm:
			return WGPUTextureFormat_ETC2RGBA8UnormSrgb;
		case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
			return WGPUTextureFormat_ETC2RGBA8Unorm;
		default:
			return WGPUTextureFormat_Undefined;
	}
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create(const TextureFormat &p_format, const TextureView &p_view) {
	WGTexture *tex = new WGTexture();

	tex->format = _data_format_to_wgpu(p_format.format);
	tex->rd_format = p_format.format;
	tex->dimension = _texture_type_to_dimension(p_format.texture_type);
	tex->view_dimension = _texture_type_to_view_dimension(p_format.texture_type);
	tex->width = p_format.width;
	tex->height = p_format.height;
	tex->depth = p_format.depth;
	tex->mipmaps = p_format.mipmaps;
	tex->layers = p_format.array_layers;
	tex->sample_count = 1 << p_format.samples;
	tex->usage = _texture_usage_to_wgpu(p_format.usage_bits);

	// WebGPU does not support R8/RG8/R16/RG16 as storage texel formats.
	// Upgrade to 32-bit equivalents when storage binding is needed.
	if (tex->usage & WGPUTextureUsage_StorageBinding) {
		tex->format = _promote_storage_format(tex->format);
		// When readonly-and-readwrite-storage-textures is unavailable, read and
		// read_write storage textures are converted to sampled textures in the
		// shader. We need CopySrc (for read_write shadow copies) and
		// TextureBinding (so the texture can be bound as a sampled texture
		// for read-only storage textures converted to texture_2d).
		if (!has_rw_storage_textures) {
			tex->usage |= WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
		}
	}

	// When float32-filterable is not supported (e.g. Adreno GPU), float32
	// textures cannot use linear filtering and get substituted with blank
	// fallbacks at bind time, losing all data.  Downgrade to float16 which
	// is universally filterable and preserves the data with sufficient
	// precision.  Only target textures that are not storage/render-attachment/
	// multisampled (those need exact format matching with compute shaders).
	if (!float32_filterable_supported &&
			!(tex->usage & WGPUTextureUsage_StorageBinding) &&
			!(tex->usage & WGPUTextureUsage_RenderAttachment) &&
			tex->sample_count == 1) {
		if (tex->format == WGPUTextureFormat_RGBA32Float) {
			tex->format = WGPUTextureFormat_RGBA16Float;
			WEBGPU_DIAG({ console.log('[F32-DOWNGRADE] RGBA32Float→RGBA16Float size=' + $0 + 'x' + $1 + ' usage=0x' + ($2).toString(16)); }, (int)tex->width, (int)tex->height, (int)tex->usage);
		} else if (tex->format == WGPUTextureFormat_RG32Float) {
			tex->format = WGPUTextureFormat_RG16Float;
			WEBGPU_DIAG({ console.log('[F32-DOWNGRADE] RG32Float→RG16Float size=' + $0 + 'x' + $1 + ' usage=0x' + ($2).toString(16)); }, (int)tex->width, (int)tex->height, (int)tex->usage);
		} else if (tex->format == WGPUTextureFormat_R32Float) {
			tex->format = WGPUTextureFormat_R16Float;
			WEBGPU_DIAG({ console.log('[F32-DOWNGRADE] R32Float→R16Float size=' + $0 + 'x' + $1 + ' usage=0x' + ($2).toString(16)); }, (int)tex->width, (int)tex->height, (int)tex->usage);
		}
	}

	// WebGPU has no texture component swizzle (unlike Vulkan's VkComponentSwizzle).
	// Textures with non-identity swizzles (e.g., R8 with R→all broadcast) must be
	// handled at a higher level. The driver stores the swizzle for potential future
	// use but does NOT promote the format (Godot's upload data wouldn't match).
	// See: servers/rendering/renderer_rd/storage_rd/texture_storage.cpp which
	// handles swizzle emulation for backends that don't support it natively.

	WGPUTextureDescriptor desc = {};
	desc.dimension = tex->dimension;
	desc.format = tex->format;
	desc.size.width = tex->width;
	desc.size.height = tex->height;
	desc.size.depthOrArrayLayers = (tex->dimension == WGPUTextureDimension_3D) ? tex->depth : tex->layers;
	desc.mipLevelCount = tex->mipmaps;
	desc.sampleCount = tex->sample_count;
	desc.usage = tex->usage;

	// Allow reinterpretation between sRGB and linear variants via texture views.
	// sRGB viewFormats are incompatible with StorageBinding in WebGPU/Dawn,
	// so skip adding them for storage textures.
	WGPUTextureFormat srgb_compat = _get_srgb_view_format(tex->format);
	if (srgb_compat != WGPUTextureFormat_Undefined && !(tex->usage & WGPUTextureUsage_StorageBinding)) {
		desc.viewFormatCount = 1;
		desc.viewFormats = &srgb_compat;
	}

	// Set a fallback label in the descriptor BEFORE creation so Dawn's error
	// formatter picks it up — Chrome/Dawn uses descriptor.label at creation time
	// for error messages, NOT the mutable GPUObjectBase.label property that
	// wgpuTextureSetLabel updates. Includes a monotonic counter so separate
	// allocations with identical dimensions are distinguishable in the logs.
	static uint32_t _tex_create_counter = 0;
	uint32_t _tex_id = _tex_create_counter++;
	char _tex_fallback[128];
	snprintf(_tex_fallback, sizeof(_tex_fallback), "unnamed#%u %ux%ux%u mip%u fmt%d usage0x%x",
			_tex_id, tex->width, tex->height, tex->layers, tex->mipmaps, (int)tex->format, (unsigned)tex->usage);
	desc.label = { _tex_fallback, WGPU_STRLEN };

	tex->handle = wgpuDeviceCreateTexture(device, &desc);
	if (tex->handle == nullptr) {
		delete tex;
		ERR_FAIL_V(TextureID());
	}
	tex->view_source = tex->handle; // Always the owning WGPUTexture; inherited by shared/sliced textures.
	tex->debug_create_id = _tex_id;

	// Create default view.
	WGPUTextureViewDescriptor view_desc = {};
	view_desc.format = tex->format;
	view_desc.dimension = tex->view_dimension;
	view_desc.baseMipLevel = 0;
	view_desc.mipLevelCount = tex->mipmaps;
	view_desc.baseArrayLayer = 0;
	view_desc.arrayLayerCount = tex->layers;
	view_desc.aspect = WGPUTextureAspect_All;

	tex->default_view = wgpuTextureCreateView(tex->handle, &view_desc);
	if (tex->default_view == nullptr) {
		wgpuTextureRelease(tex->handle);
		delete tex;
		ERR_FAIL_V_MSG(TextureID(), "WebGPU: wgpuTextureCreateView failed for default view.");
	}

	return TextureID(tex);
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_from_extension(uint64_t p_native_texture, TextureType p_type, DataFormat p_format, uint32_t p_array_layers, bool p_depth_stencil, uint32_t p_mipmaps) {
	// Not supported on web platform.
	ERR_FAIL_V_MSG(TextureID(), "WebGPU: texture_create_from_extension not supported.");
}

// Returns true if the format is an sRGB variant.
static bool _is_srgb_format(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_RGBA8UnormSrgb:
		case WGPUTextureFormat_BGRA8UnormSrgb:
		case WGPUTextureFormat_BC1RGBAUnormSrgb:
		case WGPUTextureFormat_BC2RGBAUnormSrgb:
		case WGPUTextureFormat_BC3RGBAUnormSrgb:
		case WGPUTextureFormat_ETC2RGB8UnormSrgb:
		case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
		case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
			return true;
		default:
			return false;
	}
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_shared(TextureID p_original_texture, const TextureView &p_view) {
	WGTexture *orig = (WGTexture *)(p_original_texture.id);
	ERR_FAIL_NULL_V(orig, TextureID());

	WGTexture *tex = new WGTexture();
	*tex = *orig; // Copy base properties.

	// Create a new view with potentially different format.
	WGPUTextureViewDescriptor view_desc = {};
	if (p_view.format != DATA_FORMAT_MAX) {
		view_desc.format = _data_format_to_wgpu(p_view.format);
		// texture_create() promotes narrow storage-incapable formats (R16Float,
		// R16Uint, ...) to their 32-bit equivalents on the real underlying
		// WGPUTexture (_promote_storage_format()). A view's format must match
		// that real texture format exactly -- an unpromoted view request here
		// (e.g. SSAO/SSIL's R16_SFLOAT storage textures) produces "the texture
		// view format (R16Float) is not compatible with the texture format
		// (R32Float)". Apply the same promotion so this stays in sync.
		if (tex->usage & WGPUTextureUsage_StorageBinding) {
			view_desc.format = _promote_storage_format(view_desc.format);
		}
		tex->format = view_desc.format;
		tex->rd_format = p_view.format;
	} else {
		view_desc.format = orig->format;
	}
	view_desc.dimension = tex->view_dimension;
	view_desc.baseMipLevel = 0;
	view_desc.mipLevelCount = tex->mipmaps;
	view_desc.baseArrayLayer = 0;
	view_desc.arrayLayerCount = tex->layers;
	view_desc.aspect = WGPUTextureAspect_All;

	// sRGB formats are incompatible with StorageBinding in WebGPU.
	// If creating an sRGB view of a storage texture, fall back to linear format.
	if (_is_srgb_format(view_desc.format) && (orig->usage & WGPUTextureUsage_StorageBinding)) {
		view_desc.format = orig->format;
		tex->format = orig->format;
	}

	// view_source was already inherited from orig via *tex = *orig.
	if (tex->view_source == nullptr) {
		delete tex;
		ERR_FAIL_V_MSG(TextureID(), "WebGPU: texture_create_shared: original texture has no GPU handle (view_source is null).");
	}
	tex->default_view = wgpuTextureCreateView(tex->view_source, &view_desc);
	if (tex->default_view == nullptr) {
		delete tex;
		ERR_FAIL_V_MSG(TextureID(), "WebGPU: wgpuTextureCreateView failed for shared texture view.");
	}
	tex->handle = nullptr; // Shared texture does not own the WGPUTexture.

	return TextureID(tex);
}

RDD::TextureID RenderingDeviceDriverWebGPU::texture_create_shared_from_slice(TextureID p_original_texture, const TextureView &p_view, TextureSliceType p_slice_type, uint32_t p_layer, uint32_t p_layers, uint32_t p_mipmap, uint32_t p_mipmaps) {
	WGTexture *orig = (WGTexture *)(p_original_texture.id);
	ERR_FAIL_NULL_V(orig, TextureID());

	WGTexture *tex = new WGTexture();
	*tex = *orig;

	WGPUTextureViewDescriptor view_desc = {};
	view_desc.format = (p_view.format != DATA_FORMAT_MAX) ? _data_format_to_wgpu(p_view.format) : orig->format;
	if (p_view.format != DATA_FORMAT_MAX) {
		// Mirror texture_create_shared()'s promotion of narrow storage-incapable
		// formats (R16Float, R16Uint, ...) to their real 32-bit equivalents --
		// an unpromoted view request here against an already-promoted underlying
		// texture produces "the texture view format (R16Float) is not compatible
		// with the texture format (R32Float)".
		if (tex->usage & WGPUTextureUsage_StorageBinding) {
			view_desc.format = _promote_storage_format(view_desc.format);
		}
		tex->rd_format = p_view.format;
		tex->format = view_desc.format;
	}
	view_desc.baseMipLevel = p_mipmap;
	view_desc.mipLevelCount = p_mipmaps;
	view_desc.baseArrayLayer = p_layer;
	view_desc.arrayLayerCount = p_layers;
	view_desc.aspect = WGPUTextureAspect_All;

	switch (p_slice_type) {
		case TEXTURE_SLICE_2D:
			view_desc.dimension = WGPUTextureViewDimension_2D;
			break;
		case TEXTURE_SLICE_CUBEMAP:
			view_desc.dimension = WGPUTextureViewDimension_Cube;
			break;
		case TEXTURE_SLICE_3D:
			view_desc.dimension = WGPUTextureViewDimension_3D;
			break;
		case TEXTURE_SLICE_2D_ARRAY:
			view_desc.dimension = WGPUTextureViewDimension_2DArray;
			break;
		default:
			view_desc.dimension = orig->view_dimension;
			break;
	}
	// Unlike Vulkan/Metal (VK_IMAGE_VIEW_TYPE_2D + 2D_ARRAY_COMPATIBLE), WebGPU
	// has no way to view a single Z-slice of a 3D-dimension texture as a 2D
	// view at all -- a view's dimension must structurally match the real
	// texture's own WGPUTextureDimension (2D-family texture -> 2D/2DArray/
	// Cube/CubeArray view; 3D texture -> 3D view only, covering the whole
	// depth range). A caller requesting TEXTURE_SLICE_2D (or any non-3D slice
	// type) against a genuinely 3D-dimension source -- which several
	// engine-side call sites of texture_create_shared_from_slice() do
	// generically, not knowing whether the texture they were handed is 2D or
	// 3D -- would otherwise attempt an outright-invalid wgpuTextureCreateView
	// call ("dimension ... is not compatible with the dimension ... of
	// [Texture]"), the same structural-incompatibility class as the uniform-
	// binding case fixed in Task 9.5 Round 6, just at texture-slice-creation
	// time instead of bind-group-creation time. Keep a full 3D view instead
	// of crashing; the caller's chosen Z-slice addressing (baseArrayLayer/
	// arrayLayerCount below) has no WebGPU-view equivalent for a 3D texture,
	// so slice isolation is lost here -- a real, documented limitation, not a
	// complete fix -- but this is safe by construction and strictly better
	// than a hard validation error. See webgpu_notes/TASKS.md Task 9.5 Round 8.
	if (orig->dimension == WGPUTextureDimension_3D && view_desc.dimension != WGPUTextureViewDimension_3D) {
		view_desc.dimension = WGPUTextureViewDimension_3D;
		view_desc.baseArrayLayer = 0;
		view_desc.arrayLayerCount = 1;
	}

	// view_source was already inherited from orig via *tex = *orig.
	if (tex->view_source == nullptr) {
		delete tex;
		ERR_FAIL_V_MSG(TextureID(), "WebGPU: texture_create_shared_from_slice: original texture has no GPU handle (view_source is null).");
	}

	// sRGB formats are incompatible with StorageBinding in WebGPU.
	// Fall back to the parent's linear format if sRGB is requested on a storage texture.
	if (_is_srgb_format(view_desc.format) && (orig->usage & WGPUTextureUsage_StorageBinding)) {
		view_desc.format = orig->format;
		tex->format = orig->format;
	}

	tex->default_view = wgpuTextureCreateView(tex->view_source, &view_desc);
	if (tex->default_view == nullptr) {
		delete tex;
		ERR_FAIL_V_MSG(TextureID(), "WebGPU: wgpuTextureCreateView failed for sliced texture view.");
	}
	tex->handle = nullptr;
	tex->layers = p_layers;
	tex->mipmaps = p_mipmaps;

	// Update slice-specific metadata so downstream code (uniform set creation,
	// dimension fixups, allocation queries, subresource tracking) sees the
	// correct subresource instead of the parent's full extent.
	//
	// Bug this fixes: without this, tex->view_dimension still says 2DArray
	// (inherited from parent), so uniform_set_create's SAMPLER_WITH_TEXTURE
	// fixup thinks the slice doesn't match shader-expected 2D and re-creates
	// a view from view_source with baseMipLevel=0/baseArrayLayer=0 — i.e.
	// ignoring the slice and sampling the parent's mip 0 layer 0.
	tex->view_dimension = view_desc.dimension;

	// Track the slice's base offset into the parent, so future dimension
	// fixups can create correct subresource views rather than resetting to 0.
	tex->base_mipmap = p_mipmap;
	tex->base_layer = p_layer;

	// Update width/height to the mip level's extent so allocation-size and
	// similar queries return values for the actual subresource.
	for (uint32_t m = 0; m < p_mipmap; m++) {
		tex->width = MAX(1u, tex->width >> 1);
		tex->height = MAX(1u, tex->height >> 1);
		if (tex->depth > 1) {
			tex->depth = MAX(1u, tex->depth >> 1);
		}
	}

	return TextureID(tex);
}

void RenderingDeviceDriverWebGPU::texture_free(TextureID p_texture) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(tex);

	// Drop any readback-cache entries keyed on this texture before its
	// pointer can be recycled by a future allocation. The cache key is
	// (uintptr_t)tex ^ (layer << 48) — see texture_get_data — so we mask
	// the high 16 bits when comparing to match across all layers.
	// If an async map is still in flight (!map_complete), we can't free the
	// entry here — the callback holds a pointer to it. Instead, mark it
	// cancelled; the callback will release the staging buffer/shadow and
	// delete the entry when it fires.
	{
		const uint64_t LAYER_MASK = 0xFFFF000000000000ULL;
		const uint64_t base = (uint64_t)(uintptr_t)tex;
		LocalVector<uint64_t> to_remove;
		for (KeyValue<uint64_t, ReadbackEntry *> &kv : _readback_cache) {
			if ((kv.key & ~LAYER_MASK) == (base & ~LAYER_MASK)) {
				ReadbackEntry *entry = kv.value;
				if (!entry->map_complete) {
					// Async map still in flight — let the callback clean up.
					entry->cancelled = true;
				} else {
					if (entry->staging) {
						wgpuBufferRelease(entry->staging);
					}
					if (entry->shadow) {
						memfree(entry->shadow);
					}
					memdelete(entry);
				}
				to_remove.push_back(kv.key);
			}
		}
		for (uint64_t k : to_remove) {
			_readback_cache.erase(k);
		}
	}

	// Remove any rw_shadow_copy_map entries keyed on this texture's handle,
	// so stale handles can't match a recycled WGPUTexture later.
	if (tex->handle) {
		rw_shadow_copy_map.erase(tex->handle);
	}

	if (tex->default_view) {
		wgpuTextureViewRelease(tex->default_view);
	}
	if (tex->handle && !tex->is_from_swap_chain) {
		wgpuTextureRelease(tex->handle);
	}
	delete tex;
}

uint64_t RenderingDeviceDriverWebGPU::texture_get_allocation_size(TextureID p_texture) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL_V(tex, 0);
	// Use Godot's format-aware helper (handles compressed block sizes, mipmaps,
	// and per-format bpp). Fall back to a conservative bpp=4 estimate when the
	// rd_format wasn't stored (shared/sliced views, etc.).
	if (tex->rd_format != DATA_FORMAT_MAX) {
		uint64_t layer_size = get_image_format_required_size(tex->rd_format, tex->width, tex->height, tex->depth, tex->mipmaps);
		return layer_size * tex->layers;
	}
	return (uint64_t)tex->width * tex->height * tex->depth * tex->layers * 4;
}

void RenderingDeviceDriverWebGPU::texture_get_copyable_layout(TextureID p_texture, const TextureSubresource &p_subresource, TextureCopyableLayout *r_layout) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(tex);
	ERR_FAIL_NULL(r_layout);

	uint32_t mip_width = MAX(1u, tex->width >> p_subresource.mipmap);
	uint32_t mip_height = MAX(1u, tex->height >> p_subresource.mipmap);
	uint32_t row_bytes = 0;
	uint32_t rows_for_size = mip_height;

	if (tex->rd_format != DATA_FORMAT_MAX) {
		// Compressed formats (BCn / ETC2 / ASTC) pack their data in fixed-size
		// blocks. Row pitch is (blocks_wide * block_byte_size) and "height" is
		// measured in blocks for the copy layout.
		uint32_t block_w = 1, block_h = 1;
		get_compressed_image_format_block_dimensions(tex->rd_format, block_w, block_h);
		if (block_w > 1 || block_h > 1) {
			uint32_t block_byte_size = get_compressed_image_format_block_byte_size(tex->rd_format);
			uint32_t blocks_wide = (mip_width + block_w - 1) / block_w;
			uint32_t blocks_tall = (mip_height + block_h - 1) / block_h;
			row_bytes = blocks_wide * block_byte_size;
			rows_for_size = blocks_tall;
		} else {
			// Use the actual GPU format's pixel size, not the original rd_format.
			// R8/RG8/R16/RG16 textures with STORAGE usage are promoted to 32-bit
			// equivalents (e.g. R8Unorm → R32Float), so the GPU-side bpp differs.
			row_bytes = mip_width * wgpu_format_pixel_size(tex->format);
		}
	} else {
		row_bytes = mip_width * 4; // Conservative fallback.
	}

	// WebGPU requires 256-byte row alignment for buffer <-> texture copies.
	r_layout->row_pitch = ((row_bytes + 255) / 256) * 256;
	r_layout->size = (uint64_t)r_layout->row_pitch * rows_for_size;
}

Vector<uint8_t> RenderingDeviceDriverWebGPU::texture_get_data(TextureID p_texture, uint32_t p_layer) {
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL_V(tex, Vector<uint8_t>());

	// Same frame-deferred readback pattern as buffer_get_data_direct.
	// First call: copy texture → staging buffer, initiate async map, return zeros.
	// Subsequent calls: return cached data from completed async map.

	uint64_t key = (uint64_t)(uintptr_t)tex ^ ((uint64_t)p_layer << 48);
	ReadbackEntry *entry = nullptr;

	if (_readback_cache.has(key)) {
		entry = _readback_cache[key];
	}

	// Two pixel sizes: the original Godot format (rd_bpp) for the output
	// Vector, and the actual GPU format (gpu_bpp) for staging buffer sizing
	// and the CopyTextureToBuffer stride. These diverge when the driver
	// promotes storage formats (R8→R32Float) or downgrades float32→float16.
	uint32_t rd_bpp = (tex->rd_format != DATA_FORMAT_MAX) ? get_image_format_pixel_size(tex->rd_format) : 4;
	if (rd_bpp == 0) {
		rd_bpp = 4;
	}
	uint32_t gpu_bpp = wgpu_format_pixel_size(tex->format);
	if (gpu_bpp == 0) {
		gpu_bpp = rd_bpp;
	}
	uint32_t mip_w = tex->width;
	uint32_t mip_h = tex->height;
	uint32_t row_pitch = ((mip_w * gpu_bpp + 255) / 256) * 256; // 256-byte aligned
	uint64_t buffer_size = (uint64_t)row_pitch * mip_h;

	// If a readback is in flight, deliver pending callbacks and then probe the
	// buffer's map state directly. emdawnwebgpu may have completed the map at
	// the JS level even though the C callback hasn't been delivered yet (same
	// trick as buffer_map). If mapped, copy data into shadow and mark complete.
	if (entry && !entry->map_complete && entry->map_pending) {
		WGPUInstance inst = context_driver ? context_driver->get_instance() : nullptr;
		if (inst) {
			wgpuInstanceProcessEvents(inst);
		}
		if (!entry->map_complete) {
			WGPUBufferMapState state = wgpuBufferGetMapState(entry->staging);
			if (state == WGPUBufferMapState_Mapped) {
				const void *mapped = wgpuBufferGetConstMappedRange(entry->staging, 0, entry->size);
				if (mapped && entry->shadow) {
					memcpy(entry->shadow, mapped, entry->size);
					entry->has_data = true;
				}
				wgpuBufferUnmap(entry->staging);
				entry->map_pending = false;
				entry->map_complete = true;
			}
		}
	}

	// Return cached data if previous readback completed.
	if (entry && entry->has_data && entry->map_complete) {
		Vector<uint8_t> result;
		result.resize(mip_w * mip_h * rd_bpp);
		uint8_t *dst = result.ptrw();

		if (gpu_bpp != rd_bpp) {
			// Format was promoted or downgraded — convert GPU data back to
			// the original Godot format (e.g. R32Float→R8, Float16→Float32).
			uint32_t dst_pitch = mip_w * rd_bpp;
			texture_readback_convert(p_texture, entry->shadow, row_pitch,
					dst, dst_pitch, mip_w, mip_h);
		} else {
			// No format divergence — un-pad from staging to tightly packed output.
			uint32_t tight_row = mip_w * rd_bpp;
			for (uint32_t y = 0; y < mip_h; y++) {
				memcpy(dst + y * tight_row, entry->shadow + y * row_pitch, tight_row);
			}
		}

		// Mark consumed; do NOT auto-requeue. Each top-level call (e.g.
		// viewport.get_texture().get_image()) should reflect GPU state at call
		// time, not the previous call's snapshot. The next call to
		// texture_get_data falls through to the queue-fresh-copy path below.
		// For continuous readback the caller can simply call again next frame
		// and will get a fresh capture (one-frame extra latency, but always
		// current). Earlier streaming-style auto-requeue here caused
		// scroll-screenshot to show t_(n-1) on subsequent scrolls because the
		// post-Path-A copy snapshotted the *post-scroll* frame, not the next
		// pre-scroll frame.
		entry->has_data = false;

		return result;
	}

	// First call — create staging buffer and initiate readback.
	if (!entry) {
		entry = memnew(ReadbackEntry);
		entry->size = buffer_size;
		entry->shadow = (uint8_t *)memalloc(buffer_size);
		memset(entry->shadow, 0, buffer_size);

		WGPUBufferDescriptor desc = {};
		desc.size = (buffer_size + 3) & ~3ULL;
		desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
		entry->staging = wgpuDeviceCreateBuffer(device, &desc);
		if (!entry->staging) {
			memfree(entry->shadow);
			memdelete(entry);
			ERR_FAIL_V_MSG(Vector<uint8_t>(), "WebGPU: texture_get_data: failed to create staging buffer.");
		}
		entry->map_complete = false;
		entry->has_data = false;

		_readback_cache[key] = entry;
	} else if (!entry->map_complete) {
		// Readback in flight from a previous call. Don't queue another copy /
		// mapAsync — wgpuBufferMapAsync on an already-pending buffer is a
		// validation error. Return empty to signal "not ready" — callers must
		// handle this (null Image) rather than receiving misleading zeros.
		return Vector<uint8_t>();
	}

	// Reaching here means: either a brand-new entry, or an existing entry
	// whose data was just consumed by Path A (has_data=false, map_complete=true).
	// In the latter case we're about to issue a fresh mapAsync, so reset
	// map_complete first — otherwise the next call's in-flight check would
	// see stale state.
	entry->map_complete = false;
	entry->map_pending = true;
	entry->has_data = false;

	// Copy texture to staging buffer.
	WGPUCommandEncoderDescriptor enc_desc = {};
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
	WGPUTexelCopyTextureInfo src_info = {};
	src_info.texture = tex->gpu_handle();
	src_info.mipLevel = 0;
	src_info.origin = { 0, 0, p_layer };
	WGPUTexelCopyBufferInfo dst_info = {};
	dst_info.buffer = entry->staging;
	dst_info.layout.offset = 0;
	dst_info.layout.bytesPerRow = row_pitch;
	dst_info.layout.rowsPerImage = mip_h;
	WGPUExtent3D extent = { mip_w, mip_h, 1 };
	wgpuCommandEncoderCopyTextureToBuffer(encoder, &src_info, &dst_info, &extent);
	WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
	wgpuQueueSubmit(queue, 1, &cmd);
	wgpuCommandBufferRelease(cmd);
	wgpuCommandEncoderRelease(encoder);

	WGPUBufferMapCallbackInfo cb = {};
	cb.mode = WGPUCallbackMode_AllowSpontaneous;
	cb.callback = _readback_map_cb;
	cb.userdata1 = entry;
	wgpuBufferMapAsync(entry->staging, WGPUMapMode_Read, 0, entry->size, cb);

	// Return empty on first call — signals "not ready" to the caller.
	// Data will be available on the next call after frame_post_draw.
	return Vector<uint8_t>();
}

BitField<RDD::TextureUsageBits> RenderingDeviceDriverWebGPU::texture_get_usages_supported_by_format(DataFormat p_format, bool p_cpu_readable) {
	// If there's no WebGPU equivalent, the format is unsupported.
	if (_data_format_to_wgpu(p_format) == WGPUTextureFormat_Undefined) {
		return 0;
	}

	// Task 7.10: R16/RG16/RGBA16 Unorm/Snorm have no native WebGPU texture format
	// (confirmed still true as of emsdk 4.0.11 -- no unorm16-texture-formats/
	// snorm16-texture-formats feature or WGPUTextureFormat_*16{Unorm,Snorm} enum
	// values exist in webgpu.h). _data_format_to_wgpu() approximates them as the
	// same-size *16Float GPU format so callers that bypass this capability check
	// (e.g. texture_can_make_shared_with_format) still get a coherent answer, but
	// that approximation is a raw bit-pattern substitute, not a real value
	// conversion: texture_upload_convert()/texture_readback_convert() only convert
	// when GPU/RD pixel sizes differ, and *16Unorm/Snorm vs *16Float are both 2
	// bytes/channel, so data would be memcpy'd as-is -- reinterpreting a 16-bit
	// normalized integer's bits as an IEEE-754 half float, producing garbage.
	// Report unsupported here instead, so callers use their own already-written
	// fallback (e.g. texture_storage.cpp's Image::FORMAT_R16/RG16/RGBA16 handling
	// reconverts to R32_SFLOAT/RG32_SFLOAT/RGBA32_SFLOAT when this returns false).
	switch (p_format) {
		case DATA_FORMAT_R16_UNORM:
		case DATA_FORMAT_R16_SNORM:
		case DATA_FORMAT_R16G16_UNORM:
		case DATA_FORMAT_R16G16_SNORM:
		case DATA_FORMAT_R16G16B16A16_UNORM:
		case DATA_FORMAT_R16G16B16A16_SNORM:
			return 0;
		default:
			break;
	}

	// These bits apply to every supported format.
	BitField<TextureUsageBits> flags = TEXTURE_USAGE_SAMPLING_BIT | TEXTURE_USAGE_CAN_UPDATE_BIT | TEXTURE_USAGE_CAN_COPY_FROM_BIT | TEXTURE_USAGE_CAN_COPY_TO_BIT;

	// Classify the format into depth/stencil, compressed, or plain color.
	bool is_depth_stencil = false;
	bool is_compressed = false;
	// Which optional compression family the format belongs to (so we can gate on
	// the corresponding adapter feature). Only meaningful when is_compressed.
	bool needs_bc = false;
	bool needs_etc2 = false;
	bool needs_astc = false;

	switch (p_format) {
		// Depth and depth/stencil formats.
		case DATA_FORMAT_D16_UNORM:
		case DATA_FORMAT_X8_D24_UNORM_PACK32:
		case DATA_FORMAT_D32_SFLOAT:
		case DATA_FORMAT_S8_UINT:
		case DATA_FORMAT_D16_UNORM_S8_UINT:
		case DATA_FORMAT_D24_UNORM_S8_UINT:
		case DATA_FORMAT_D32_SFLOAT_S8_UINT:
			is_depth_stencil = true;
			break;

		// BC compressed formats.
		case DATA_FORMAT_BC1_RGB_UNORM_BLOCK:
		case DATA_FORMAT_BC1_RGB_SRGB_BLOCK:
		case DATA_FORMAT_BC1_RGBA_UNORM_BLOCK:
		case DATA_FORMAT_BC1_RGBA_SRGB_BLOCK:
		case DATA_FORMAT_BC2_UNORM_BLOCK:
		case DATA_FORMAT_BC2_SRGB_BLOCK:
		case DATA_FORMAT_BC3_UNORM_BLOCK:
		case DATA_FORMAT_BC3_SRGB_BLOCK:
		case DATA_FORMAT_BC4_UNORM_BLOCK:
		case DATA_FORMAT_BC4_SNORM_BLOCK:
		case DATA_FORMAT_BC5_UNORM_BLOCK:
		case DATA_FORMAT_BC5_SNORM_BLOCK:
		case DATA_FORMAT_BC6H_UFLOAT_BLOCK:
		case DATA_FORMAT_BC6H_SFLOAT_BLOCK:
		case DATA_FORMAT_BC7_UNORM_BLOCK:
		case DATA_FORMAT_BC7_SRGB_BLOCK:
			is_compressed = true;
			needs_bc = true;
			break;
		// ETC2 compressed formats.
		case DATA_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
		case DATA_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
		case DATA_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
		case DATA_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
		case DATA_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
		case DATA_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
		case DATA_FORMAT_EAC_R11_UNORM_BLOCK:
		case DATA_FORMAT_EAC_R11_SNORM_BLOCK:
		case DATA_FORMAT_EAC_R11G11_UNORM_BLOCK:
		case DATA_FORMAT_EAC_R11G11_SNORM_BLOCK:
			is_compressed = true;
			needs_etc2 = true;
			break;
		// ASTC compressed formats.
		case DATA_FORMAT_ASTC_4x4_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_4x4_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_5x4_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_5x4_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_5x5_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_5x5_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_6x5_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_6x5_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_6x6_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_6x6_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_8x5_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_8x5_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_8x6_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_8x6_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_8x8_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_8x8_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_10x5_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_10x5_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_10x6_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_10x6_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_10x8_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_10x8_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_10x10_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_10x10_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_12x10_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_12x10_SRGB_BLOCK:
		case DATA_FORMAT_ASTC_12x12_UNORM_BLOCK:
		case DATA_FORMAT_ASTC_12x12_SRGB_BLOCK:
			is_compressed = true;
			needs_astc = true;
			break;

		default:
			break;
	}

	if (is_depth_stencil) {
		flags.set_flag(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
		// Depth textures can't be updated via CAN_UPDATE (CopyDst is not allowed).
		flags.clear_flag(TEXTURE_USAGE_CAN_UPDATE_BIT);
		return flags;
	}

	if (is_compressed) {
		// Gate on the corresponding optional feature — if the device didn't
		// request it, the WGPU format technically exists in the enum but any
		// attempt to create a texture with it will fail validation. Reporting
		// it as unsupported here lets Godot fall back to CPU decompression.
		if ((needs_bc && !has_texture_compression_bc) ||
				(needs_etc2 && !has_texture_compression_etc2) ||
				(needs_astc && !has_texture_compression_astc)) {
			return 0;
		}
		// Compressed textures: sampling + copy only, no render attachment, no
		// storage. CAN_UPDATE stays set — it maps to WGPUTextureUsage_CopyDst,
		// which is valid for compressed textures (uploaded via queue.writeTexture).
		return flags;
	}

	// Plain color format: render attachment is generally supported.
	flags.set_flag(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT);

	// Storage binding: only formats explicitly listed in the WebGPU spec support it.
	// See https://gpuweb.github.io/gpuweb/#storage-texel-format-capability-matrix
	// Formats not natively supported (R8, RG8, R16, RG16) are included here because
	// Godot's renderer uses them with storage; they are silently upgraded to 32-bit
	// equivalents at WGPUTexture creation time and in WGSL text.
	switch (p_format) {
		case DATA_FORMAT_R8_UNORM:
		case DATA_FORMAT_R8_SNORM:
		case DATA_FORMAT_R8G8_UNORM:
		case DATA_FORMAT_R8G8_SNORM:
		case DATA_FORMAT_R8G8B8A8_UNORM:
		case DATA_FORMAT_R8G8B8A8_SNORM:
		case DATA_FORMAT_R8G8B8A8_UINT:
		case DATA_FORMAT_R8G8B8A8_SINT:
		case DATA_FORMAT_R16_SFLOAT:
		case DATA_FORMAT_R16_SNORM:
		case DATA_FORMAT_R16_UNORM:
		// r16uint/r16sint aren't valid WGSL storage texel formats either (see the
		// "r16uint"/"r16sint" → "r32uint"/"r32sint" WGSL-text rewrites elsewhere in
		// this file) and _promote_storage_format() already promotes them to
		// R32Uint/R32Sint at texture-creation time -- these two were simply missing
		// from this capability-reporting switch, so SDFGI's Occlusion Data texture
		// (R16_UINT) failed at texture_create() before ever reaching that promotion
		// logic. Found via the user's real project after enabling SDFGI.
		case DATA_FORMAT_R16_UINT:
		case DATA_FORMAT_R16_SINT:
		case DATA_FORMAT_R16G16_SFLOAT:
		case DATA_FORMAT_R16G16_SNORM:
		case DATA_FORMAT_R16G16_UNORM:
		case DATA_FORMAT_R16G16_SINT:
		case DATA_FORMAT_R16G16_UINT:
		case DATA_FORMAT_R16G16B16A16_SFLOAT:
		case DATA_FORMAT_R16G16B16A16_UINT:
		case DATA_FORMAT_R16G16B16A16_SINT:
		case DATA_FORMAT_R32_SFLOAT:
		case DATA_FORMAT_R32_UINT:
		case DATA_FORMAT_R32_SINT:
		case DATA_FORMAT_R32G32_SFLOAT:
		case DATA_FORMAT_R32G32_UINT:
		case DATA_FORMAT_R32G32_SINT:
		case DATA_FORMAT_R32G32B32A32_SFLOAT:
		case DATA_FORMAT_R32G32B32A32_UINT:
		case DATA_FORMAT_R32G32B32A32_SINT:
		// rgb10a2unorm, rgb10a2uint, rg11b10ufloat -- part of WebGPU's storage-texel
		// format capability matrix (this build's Tint/Dawn accepts these as valid
		// textureStore() targets; see the "storage-format-string" handling for these
		// three formats elsewhere in this file, e.g. _wgsl_storage_format_string_to_wgpu()
		// from Task 9 Round 9). This switch previously omitted them, so any caller
		// (e.g. FSR2's create_resource_rd for its RG11B10_UFLOAT accumulation buffer)
		// requesting TEXTURE_USAGE_STORAGE_BIT with one of these formats got a hard
		// texture_create() failure -- which the AMD FSR2 SDK does not handle gracefully,
		// eventually manifesting as an unrelated-looking WASM "table index is out of
		// bounds" trap much later.
		case DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
		case DATA_FORMAT_A2B10G10R10_UINT_PACK32:
		case DATA_FORMAT_B10G11R11_UFLOAT_PACK32:
			flags.set_flag(TEXTURE_USAGE_STORAGE_BIT);
			break;
		default:
			break;
	}

	return flags;
}

bool RenderingDeviceDriverWebGPU::texture_can_make_shared_with_format(TextureID p_texture, DataFormat p_format, bool &r_raw_reinterpretation) {
	r_raw_reinterpretation = false;
	// WebGPU's viewFormats mechanism only ever allows a texture's own format
	// and its sRGB twin (see RD::SUPPORTS_SHAREABLE_TEXTURE_FORMATS's comment
	// in api_trait_get() below) -- texture_create() only ever declares that one
	// extra viewFormats entry (via _get_srgb_view_format()), so a real
	// wgpuTextureCreateView() with any other format would fail validation.
	// Report "not shareable" for anything but the sRGB-twin case so the caller
	// (RenderingDevice::texture_create_shared_from_slice(), see rendering_device.cpp)
	// falls back to its own independent-alias-texture path instead.
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL_V(tex, false);
	WGPUTextureFormat requested = _data_format_to_wgpu(p_format);
	return requested == tex->format || requested == _get_srgb_view_format(tex->format);
}

WGPUTextureUsage RenderingDeviceDriverWebGPU::_texture_usage_to_wgpu(BitField<TextureUsageBits> p_usage) const {
	WGPUTextureUsage flags = 0;
	if (p_usage.has_flag(TEXTURE_USAGE_SAMPLING_BIT)) {
		flags |= WGPUTextureUsage_TextureBinding;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) || p_usage.has_flag(TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
		flags |= WGPUTextureUsage_RenderAttachment;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_STORAGE_BIT) || p_usage.has_flag(TEXTURE_USAGE_STORAGE_ATOMIC_BIT)) {
		flags |= WGPUTextureUsage_StorageBinding;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_CPU_READ_BIT) || p_usage.has_flag(TEXTURE_USAGE_CAN_COPY_FROM_BIT)) {
		flags |= WGPUTextureUsage_CopySrc;
	}
	if (p_usage.has_flag(TEXTURE_USAGE_CAN_UPDATE_BIT) || p_usage.has_flag(TEXTURE_USAGE_CAN_COPY_TO_BIT)) {
		flags |= WGPUTextureUsage_CopyDst;
	}
	return flags;
}

WGPUTextureDimension RenderingDeviceDriverWebGPU::_texture_type_to_dimension(TextureType p_type) const {
	switch (p_type) {
		case TEXTURE_TYPE_1D:
			return WGPUTextureDimension_1D;
		case TEXTURE_TYPE_3D:
			return WGPUTextureDimension_3D;
		default:
			return WGPUTextureDimension_2D;
	}
}

WGPUTextureViewDimension RenderingDeviceDriverWebGPU::_texture_type_to_view_dimension(TextureType p_type) const {
	switch (p_type) {
		case TEXTURE_TYPE_1D:
			return WGPUTextureViewDimension_1D;
		case TEXTURE_TYPE_2D:
			return WGPUTextureViewDimension_2D;
		case TEXTURE_TYPE_3D:
			return WGPUTextureViewDimension_3D;
		case TEXTURE_TYPE_CUBE:
			return WGPUTextureViewDimension_Cube;
		case TEXTURE_TYPE_2D_ARRAY:
			return WGPUTextureViewDimension_2DArray;
		case TEXTURE_TYPE_CUBE_ARRAY:
			return WGPUTextureViewDimension_CubeArray;
		default:
			return WGPUTextureViewDimension_2D;
	}
}

WGPUTextureFormat RenderingDeviceDriverWebGPU::_data_format_to_wgpu(DataFormat p_format) const {
	// O(1) lookup into the static table from pixel_formats_webgpu.h.
	if ((uint32_t)p_format < (uint32_t)DATA_FORMAT_MAX) {
		WGPUTextureFormat fmt = RD_TO_WGPU_FORMAT[(uint32_t)p_format];
		if (fmt != WGPUTextureFormat_Undefined) {
			return fmt;
		}
		// The table maps some formats to Undefined that we can approximate.
		// Fall through to handle these special cases.
	}

	// Fallback approximations for formats not in the base WebGPU spec.
	switch (p_format) {
		// R16/RG16/RGBA16 Unorm/Snorm: not in emdawnwebgpu 4.0.10, use float fallback.
		case DATA_FORMAT_R16_UNORM:
		case DATA_FORMAT_R16_SNORM:
			return WGPUTextureFormat_R16Float;
		case DATA_FORMAT_R16G16_UNORM:
		case DATA_FORMAT_R16G16_SNORM:
			return WGPUTextureFormat_RG16Float;
		case DATA_FORMAT_R16G16B16A16_UNORM:
		case DATA_FORMAT_R16G16B16A16_SNORM:
			return WGPUTextureFormat_RGBA16Float;
		// No depth16+stencil8 in WebGPU; use depth24plus-stencil8 as nearest approximation.
		case DATA_FORMAT_D16_UNORM_S8_UINT:
			return WGPUTextureFormat_Depth24PlusStencil8;
		// 3-component (RGB) formats don't exist as WebGPU *texture* formats, only as
		// vertex attribute formats (handled by _data_format_to_wgpu_vertex). Return
		// Undefined silently here — texture_get_usages_supported_by_format probes every
		// DataFormat for texture-usage compat, and we don't want to spam warnings for
		// legitimate vertex-only formats.
		case DATA_FORMAT_R32G32B32_UINT:
		case DATA_FORMAT_R32G32B32_SINT:
		case DATA_FORMAT_R32G32B32_SFLOAT:
		case DATA_FORMAT_R16G16B16_UNORM:
		case DATA_FORMAT_R16G16B16_SNORM:
		case DATA_FORMAT_R16G16B16_UINT:
		case DATA_FORMAT_R16G16B16_SINT:
		case DATA_FORMAT_R16G16B16_SFLOAT:
		case DATA_FORMAT_R8G8B8_UNORM:
		case DATA_FORMAT_R8G8B8_SNORM:
		case DATA_FORMAT_R8G8B8_UINT:
		case DATA_FORMAT_R8G8B8_SINT:
			return WGPUTextureFormat_Undefined;
		default: {
			static thread_local LocalVector<int> warned_formats;
			int ifmt = (int)p_format;
			bool already_warned = false;
			for (uint32_t i = 0; i < warned_formats.size(); i++) {
				if (warned_formats[i] == ifmt) {
					already_warned = true;
					break;
				}
			}
			if (!already_warned) {
				warned_formats.push_back(ifmt);
				WARN_PRINT(vformat("WebGPU: Unsupported DataFormat %d (further occurrences suppressed)", ifmt));
			}
			return WGPUTextureFormat_Undefined;
		}
	}
}

// WebGPU does not support R8/RG8/R16/RG16 as storage texel formats. Any texture or
// render target that must carry STORAGE usage has to be created as a 32-bit variant
// instead. This helper centralizes that promotion so texture_create() and
// render_pipeline_create() stay in sync — otherwise a texture upgraded to R32Float
// gets bound to a pipeline that was built for R8Unorm and every submit fails with a
// GPUValidationError. Canvas SDF (R8_UNORM + STORAGE_BIT + COLOR_ATTACHMENT_BIT) is
// the motivating case.
WGPUTextureFormat RenderingDeviceDriverWebGPU::_promote_storage_format(WGPUTextureFormat p_format) const {
	switch (p_format) {
		// 8-bit formats: with texture-formats-tier1, these are valid storage texel
		// formats natively. Keeping the original format preserves blendable/filterable
		// properties (R32Float is not blendable on Adreno without float32-blendable).
		// The WGSL r8/rg8 replacement is also tier1-conditional, so shader and texture
		// formats stay in sync.
		case WGPUTextureFormat_R8Unorm:
		case WGPUTextureFormat_R8Snorm:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_R32Float;
		case WGPUTextureFormat_R8Uint:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_R32Uint;
		case WGPUTextureFormat_R8Sint:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_R32Sint;
		case WGPUTextureFormat_RG8Unorm:
		case WGPUTextureFormat_RG8Snorm:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_RG32Float;
		case WGPUTextureFormat_RG8Uint:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_RG32Uint;
		case WGPUTextureFormat_RG8Sint:
			if (has_texture_formats_tier1) {
				return p_format;
			}
			return WGPUTextureFormat_RG32Sint;
		// 16-bit formats: always promote. Shaders reference the 32-bit version
		// and there is no matching WGSL replacement for these.
		case WGPUTextureFormat_R16Float:
			// R16Snorm/R16Unorm not in base emdawnwebgpu 4.0.10 headers
			return WGPUTextureFormat_R32Float;
		case WGPUTextureFormat_R16Uint:
			return WGPUTextureFormat_R32Uint;
		case WGPUTextureFormat_R16Sint:
			return WGPUTextureFormat_R32Sint;
		case WGPUTextureFormat_RG16Float:
			// RG16Snorm/RG16Unorm not in base emdawnwebgpu 4.0.10 headers
			return WGPUTextureFormat_RG32Float;
		case WGPUTextureFormat_RG16Uint:
			return WGPUTextureFormat_RG32Uint;
		case WGPUTextureFormat_RG16Sint:
			return WGPUTextureFormat_RG32Sint;
		// RGBA16Snorm/RGBA16Unorm not in base emdawnwebgpu 4.0.10 headers
		default:
			return p_format;
	}
}

RDD::DataFormat RenderingDeviceDriverWebGPU::_wgpu_to_data_format(WGPUTextureFormat p_format) const {
	// Deliberately not a full reverse mapping (see webgpu_notes/TASKS.md Task
	// 7.6) -- both callers (swap_chain_get_format() and swap_chain_create()'s
	// render-pass attachment setup) only ever pass sc->format, which since
	// Task 7.11 is whichever of BGRA8Unorm/RGBA8Unorm wgpuSurfaceGetCapabilities()
	// reports as the browser's preferred canvas format -- the only two formats
	// a real caller can ever pass in here today.
	switch (p_format) {
		case WGPUTextureFormat_BGRA8Unorm:
			return DATA_FORMAT_B8G8R8A8_UNORM;
		case WGPUTextureFormat_RGBA8Unorm:
			return DATA_FORMAT_R8G8B8A8_UNORM;
		default:
			return DATA_FORMAT_MAX;
	}
}

WGPUVertexFormat RenderingDeviceDriverWebGPU::_data_format_to_wgpu_vertex(DataFormat p_format) {
	switch (p_format) {
		case DATA_FORMAT_R32_SFLOAT:
			return WGPUVertexFormat_Float32;
		case DATA_FORMAT_R32G32_SFLOAT:
			return WGPUVertexFormat_Float32x2;
		case DATA_FORMAT_R32G32B32_SFLOAT:
			return WGPUVertexFormat_Float32x3;
		case DATA_FORMAT_R32G32B32A32_SFLOAT:
			return WGPUVertexFormat_Float32x4;
		case DATA_FORMAT_R32_UINT:
			return WGPUVertexFormat_Uint32;
		case DATA_FORMAT_R32G32_UINT:
			return WGPUVertexFormat_Uint32x2;
		case DATA_FORMAT_R32G32B32_UINT:
			return WGPUVertexFormat_Uint32x3;
		case DATA_FORMAT_R32G32B32A32_UINT:
			return WGPUVertexFormat_Uint32x4;
		case DATA_FORMAT_R32_SINT:
			return WGPUVertexFormat_Sint32;
		case DATA_FORMAT_R32G32_SINT:
			return WGPUVertexFormat_Sint32x2;
		case DATA_FORMAT_R32G32B32_SINT:
			return WGPUVertexFormat_Sint32x3;
		case DATA_FORMAT_R32G32B32A32_SINT:
			return WGPUVertexFormat_Sint32x4;
		case DATA_FORMAT_R16G16_SFLOAT:
			return WGPUVertexFormat_Float16x2;
		case DATA_FORMAT_R16G16B16A16_SFLOAT:
			return WGPUVertexFormat_Float16x4;
		case DATA_FORMAT_R16G16_UINT:
			return WGPUVertexFormat_Uint16x2;
		case DATA_FORMAT_R16G16B16A16_UINT:
			return WGPUVertexFormat_Uint16x4;
		case DATA_FORMAT_R16G16_SINT:
			return WGPUVertexFormat_Sint16x2;
		case DATA_FORMAT_R16G16B16A16_SINT:
			return WGPUVertexFormat_Sint16x4;
		case DATA_FORMAT_R16G16_UNORM:
			return WGPUVertexFormat_Unorm16x2;
		case DATA_FORMAT_R16G16B16A16_UNORM:
			return WGPUVertexFormat_Unorm16x4;
		case DATA_FORMAT_R16G16_SNORM:
			return WGPUVertexFormat_Snorm16x2;
		case DATA_FORMAT_R16G16B16A16_SNORM:
			return WGPUVertexFormat_Snorm16x4;
		case DATA_FORMAT_R8G8_UNORM:
			return WGPUVertexFormat_Unorm8x2;
		case DATA_FORMAT_R8G8B8A8_UNORM:
			return WGPUVertexFormat_Unorm8x4;
		case DATA_FORMAT_R8G8_SNORM:
			return WGPUVertexFormat_Snorm8x2;
		case DATA_FORMAT_R8G8B8A8_SNORM:
			return WGPUVertexFormat_Snorm8x4;
		case DATA_FORMAT_R8G8_UINT:
			return WGPUVertexFormat_Uint8x2;
		case DATA_FORMAT_R8G8B8A8_UINT:
			return WGPUVertexFormat_Uint8x4;
		case DATA_FORMAT_R8G8_SINT:
			return WGPUVertexFormat_Sint8x2;
		case DATA_FORMAT_R8G8B8A8_SINT:
			return WGPUVertexFormat_Sint8x4;
		case DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
			return WGPUVertexFormat_Unorm10_10_10_2;
		default:
			WARN_PRINT(vformat("WebGPU: Unsupported vertex DataFormat %d", (int)p_format));
			return (WGPUVertexFormat)0;
	}
}

// =============================================================================
// SAMPLERS
// =============================================================================

RDD::SamplerID RenderingDeviceDriverWebGPU::sampler_create(const SamplerState &p_state) {
	WGPUSamplerDescriptor desc = {};

	auto map_filter = [](SamplerFilter f) -> WGPUFilterMode {
		return (f == SAMPLER_FILTER_LINEAR) ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
	};
	auto map_address = [](SamplerRepeatMode m) -> WGPUAddressMode {
		switch (m) {
			case SAMPLER_REPEAT_MODE_REPEAT:
				return WGPUAddressMode_Repeat;
			case SAMPLER_REPEAT_MODE_MIRRORED_REPEAT:
				return WGPUAddressMode_MirrorRepeat;
			default:
				return WGPUAddressMode_ClampToEdge;
		}
	};
	auto map_compare = [](CompareOperator op) -> WGPUCompareFunction {
		switch (op) {
			case COMPARE_OP_NEVER:
				return WGPUCompareFunction_Never;
			case COMPARE_OP_LESS:
				return WGPUCompareFunction_Less;
			case COMPARE_OP_EQUAL:
				return WGPUCompareFunction_Equal;
			case COMPARE_OP_LESS_OR_EQUAL:
				return WGPUCompareFunction_LessEqual;
			case COMPARE_OP_GREATER:
				return WGPUCompareFunction_Greater;
			case COMPARE_OP_NOT_EQUAL:
				return WGPUCompareFunction_NotEqual;
			case COMPARE_OP_GREATER_OR_EQUAL:
				return WGPUCompareFunction_GreaterEqual;
			case COMPARE_OP_ALWAYS:
				return WGPUCompareFunction_Always;
			default:
				return WGPUCompareFunction_Undefined;
		}
	};

	desc.magFilter = map_filter(p_state.mag_filter);
	desc.minFilter = map_filter(p_state.min_filter);
	desc.mipmapFilter = (p_state.mip_filter == SAMPLER_FILTER_LINEAR) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
	desc.addressModeU = map_address(p_state.repeat_u);
	desc.addressModeV = map_address(p_state.repeat_v);
	desc.addressModeW = map_address(p_state.repeat_w);
	// WebGPU requires 0 <= lodMinClamp <= lodMaxClamp (Vulkan/D3D12/Metal all
	// allow negative LOD clamps too, so some callers -- e.g. FSR2's sampler
	// setup, effects/fsr2.cpp, using the "-1000/1000" idiom from AMD's
	// reference Vulkan/D3D12 integration for "effectively unclamped" -- pass
	// a negative min_lod that's harmless everywhere else (there's no negative
	// mip level to clamp away, so clamping the minimum at -1000 vs. 0 is
	// behaviorally identical on every backend) but a hard validation error
	// here. Clamp defensively at the boundary rather than touching every
	// shared caller's sampler-state setup. See webgpu_notes/TASKS.md Task 9.5.
	desc.lodMinClamp = MAX(p_state.min_lod, 0.0f);
	desc.lodMaxClamp = MAX(p_state.max_lod, desc.lodMinClamp);

	if (p_state.enable_compare) {
		desc.compare = map_compare(p_state.compare_op);
	}

	if (p_state.use_anisotropy) {
		desc.maxAnisotropy = (uint16_t)p_state.anisotropy_max;
		// WebGPU requires minFilter, magFilter, and mipmapFilter to all be
		// Linear when maxAnisotropy > 1.
		if (desc.maxAnisotropy > 1) {
			desc.minFilter = WGPUFilterMode_Linear;
			desc.magFilter = WGPUFilterMode_Linear;
			desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
		}
	} else {
		desc.maxAnisotropy = 1;
	}

	WGPUSampler sampler = wgpuDeviceCreateSampler(device, &desc);
	ERR_FAIL_COND_V(sampler == nullptr, SamplerID());

	// Store the WGPUSampler handle directly as the ID (no wrapper struct needed).
	return SamplerID((uint64_t)sampler);
}

void RenderingDeviceDriverWebGPU::sampler_free(SamplerID p_sampler) {
	WGPUSampler sampler = (WGPUSampler)(p_sampler.id);
	if (sampler) {
		wgpuSamplerRelease(sampler);
	}
}

bool RenderingDeviceDriverWebGPU::sampler_is_format_supported_for_filter(DataFormat p_format, SamplerFilter p_filter) {
	if (p_filter == SAMPLER_FILTER_NEAREST) {
		return true; // All formats support nearest filtering.
	}
	// Linear filtering rules on WebGPU:
	//   - 32-bit float formats (R32F / RG32F / RGBA32F) require the "float32-filterable"
	//     optional feature. Without it the sampler must use NEAREST.
	//   - Integer formats (UINT / SINT) are never filterable in any API.
	//   - All other formats (8/16-bit normalized, 16F, SRGB, BCn/ETC2/ASTC, depth) are
	//     filterable on any spec-compliant WebGPU implementation.
	switch (p_format) {
		// 32-bit float formats — gated on the float32-filterable optional feature.
		case DATA_FORMAT_R32_SFLOAT:
		case DATA_FORMAT_R32G32_SFLOAT:
		case DATA_FORMAT_R32G32B32_SFLOAT:
		case DATA_FORMAT_R32G32B32A32_SFLOAT:
			return float32_filterable_supported;

		// Integer formats — never filterable.
		case DATA_FORMAT_R8_UINT:
		case DATA_FORMAT_R8_SINT:
		case DATA_FORMAT_R8G8_UINT:
		case DATA_FORMAT_R8G8_SINT:
		case DATA_FORMAT_R8G8B8A8_UINT:
		case DATA_FORMAT_R8G8B8A8_SINT:
		case DATA_FORMAT_R16_UINT:
		case DATA_FORMAT_R16_SINT:
		case DATA_FORMAT_R16G16_UINT:
		case DATA_FORMAT_R16G16_SINT:
		case DATA_FORMAT_R16G16B16A16_UINT:
		case DATA_FORMAT_R16G16B16A16_SINT:
		case DATA_FORMAT_R32_UINT:
		case DATA_FORMAT_R32_SINT:
		case DATA_FORMAT_R32G32_UINT:
		case DATA_FORMAT_R32G32_SINT:
		case DATA_FORMAT_R32G32B32A32_UINT:
		case DATA_FORMAT_R32G32B32A32_SINT:
			return false;

		default:
			return true;
	}
}

// =============================================================================
// VERTEX FORMAT
// =============================================================================

RDD::VertexFormatID RenderingDeviceDriverWebGPU::vertex_format_create(Span<VertexAttribute> p_vertex_attribs, const VertexAttributeBindingsMap &p_vertex_bindings) {
	WGVertexFormat *vf = new WGVertexFormat();

	// Build attribute list.
	for (uint32_t i = 0; i < p_vertex_attribs.size(); i++) {
		const VertexAttribute &va = p_vertex_attribs[i];
		WGVertexFormat::Attribute attr;
		attr.location = va.location;
		attr.format = _data_format_to_wgpu_vertex(va.format);
		attr.offset = va.offset;
		attr.binding = (va.binding == UINT32_MAX) ? i : va.binding;
		vf->attributes.push_back(attr);
	}

	// Build binding list from p_vertex_bindings map.
	if (!p_vertex_bindings.is_empty()) {
		uint32_t max_binding = 0;
		for (const KeyValue<uint32_t, VertexAttributeBinding> &kv : p_vertex_bindings) {
			max_binding = MAX(max_binding, kv.key);
		}
		vf->bindings.resize(max_binding + 1);
		for (const KeyValue<uint32_t, VertexAttributeBinding> &kv : p_vertex_bindings) {
			vf->bindings[kv.key].stride = kv.value.stride;
			vf->bindings[kv.key].step_mode = (kv.value.frequency == VERTEX_FREQUENCY_INSTANCE)
					? WGPUVertexStepMode_Instance
					: WGPUVertexStepMode_Vertex;
		}
	} else {
		// Build bindings from vertex attribute data (stride/frequency stored per-attribute in older API).
		HashMap<uint32_t, bool> seen;
		for (uint32_t i = 0; i < p_vertex_attribs.size(); i++) {
			const VertexAttribute &va = p_vertex_attribs[i];
			uint32_t binding = (va.binding == UINT32_MAX) ? i : va.binding;
			if (!seen.has(binding)) {
				if (vf->bindings.size() <= binding) {
					vf->bindings.resize(binding + 1);
				}
				vf->bindings[binding].stride = va.stride;
				vf->bindings[binding].step_mode = (va.frequency == VERTEX_FREQUENCY_INSTANCE)
						? WGPUVertexStepMode_Instance
						: WGPUVertexStepMode_Vertex;
				seen[binding] = true;
			}
		}
	}

	return VertexFormatID(vf);
}

void RenderingDeviceDriverWebGPU::vertex_format_free(VertexFormatID p_vertex_format) {
	WGVertexFormat *vf = (WGVertexFormat *)(p_vertex_format.id);
	delete vf;
}

// =============================================================================
// BARRIERS (ALL NO-OPS)
// =============================================================================

void RenderingDeviceDriverWebGPU::command_pipeline_barrier(
		CommandBufferID p_cmd_buffer,
		BitField<PipelineStageBits> p_src_stages,
		BitField<PipelineStageBits> p_dst_stages,
		VectorView<MemoryAccessBarrier> p_memory_barriers,
		VectorView<BufferBarrier> p_buffer_barriers,
		VectorView<TextureBarrier> p_texture_barriers,
		VectorView<AccelerationStructureBarrier> p_acceleration_structure_barriers) {
	// No-op: WebGPU handles synchronization automatically. Acceleration structures
	// are not supported by this driver (no ray tracing on WebGPU).
}

// =============================================================================
// RAY TRACING (UNSUPPORTED — WebGPU has no ray tracing API)
// =============================================================================
// RenderingDevice gates all of these behind has_feature(SUPPORTS_RAYTRACING_PIPELINE)
// and has_feature(SUPPORTS_RAY_QUERY), both false for this driver (see has_feature()
// below), so these should never be called in practice.
// =============================================================================

RDD::AccelerationStructureID RenderingDeviceDriverWebGPU::blas_create(VectorView<AccelerationStructureGeometry> p_geometries, BitField<AccelerationStructureFlagBits> p_flags) {
	ERR_FAIL_V_MSG(AccelerationStructureID(), "Ray tracing is not supported by the WebGPU rendering driver.");
}

RDD::AccelerationStructureID RenderingDeviceDriverWebGPU::tlas_create(uint32_t p_max_instance_count, BitField<AccelerationStructureFlagBits> p_flags) {
	ERR_FAIL_V_MSG(AccelerationStructureID(), "Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::acceleration_structure_instance_write(uint8_t *r_driver_instance, const AccelerationStructureInstance &p_instance) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::acceleration_structure_free(AccelerationStructureID p_acceleration_structure) {
	// No-op: nothing was ever created.
}

uint32_t RenderingDeviceDriverWebGPU::acceleration_structure_get_scratch_size_bytes(AccelerationStructureID p_acceleration_structure) {
	return 0;
}

RDD::RaytracingPipelineID RenderingDeviceDriverWebGPU::raytracing_pipeline_create(VectorView<PipelineShader> p_shaders, VectorView<uint32_t> p_raygen_shader_indices, VectorView<uint32_t> p_miss_shader_indices, VectorView<HitGroup> p_hit_groups, uint32_t p_max_trace_recursion_depth, ShaderID p_layout_defining_shader) {
	ERR_FAIL_V_MSG(RaytracingPipelineID(), "Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::raytracing_pipeline_free(RaytracingPipelineID p_pipeline) {
	// No-op: nothing was ever created.
}

bool RenderingDeviceDriverWebGPU::raytracing_pipeline_get_shader_group_handles(RaytracingPipelineID p_pipeline, uint32_t p_group_index_offset, VectorView<uint32_t> p_group_indices, uint8_t *r_data, uint32_t p_data_stride_bytes) {
	ERR_FAIL_V_MSG(false, "Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::command_build_blas(CommandBufferID p_cmd_buffer, AccelerationStructureID p_acceleration_structure, BufferID p_scratch_buffer) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::command_build_tlas(CommandBufferID p_cmd_buffer, AccelerationStructureID p_acceleration_structure, BufferID p_scratch_buffer, BufferID p_instance_buffer, uint32_t p_instance_offset, uint32_t p_instance_count) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::command_bind_raytracing_pipeline(CommandBufferID p_cmd_buffer, RaytracingPipelineID p_pipeline) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::command_bind_raytracing_uniform_set(CommandBufferID p_cmd_buffer, UniformSetID p_uniform_set, ShaderID p_shader, uint32_t p_set_index) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

void RenderingDeviceDriverWebGPU::command_trace_rays(CommandBufferID p_cmd_buffer, const ShaderBindingTable &p_raygen_sbt, const ShaderBindingTable &p_miss_sbt, const ShaderBindingTable &p_hit_sbt, uint32_t p_width, uint32_t p_height, uint32_t p_depth) {
	ERR_FAIL_MSG("Ray tracing is not supported by the WebGPU rendering driver.");
}

// =============================================================================
// FENCES
// =============================================================================

RDD::FenceID RenderingDeviceDriverWebGPU::fence_create() {
	WGFence *fence = new WGFence();
	return FenceID(fence);
}

Error RenderingDeviceDriverWebGPU::fence_wait(FenceID p_fence) {
	WGFence *fence = (WGFence *)(p_fence.id);
	ERR_FAIL_NULL_V(fence, ERR_INVALID_PARAMETER);

	// In the browser's single-threaded model, GPU work submitted via
	// wgpuQueueSubmit completes asynchronously.  The emdawnwebgpu
	// implementation resolves AllowSpontaneous callbacks during
	// wgpuInstanceProcessEvents.  Poll the instance to allow the
	// work-done callback (registered in command_queue_execute_and_present)
	// to fire and set fence->signaled = true.
	WGPUInstance inst = context_driver ? context_driver->get_instance() : nullptr;
	if (inst) {
		wgpuInstanceProcessEvents(inst);
	}

	// On single-threaded WASM, the callback may not fire until the next
	// browser event loop tick. Between frames the GPU typically completes
	// the previous frame's work, so the callback fires during processEvents
	// above. If it hasn't fired yet, we force-signal to avoid deadlock —
	// the engine calls fence_wait at frame start for the *previous* frame's
	// fence, so the GPU has had a full frame duration to finish.
	if (!fence->signaled) {
		fence->signaled = true;
	}
	return OK;
}

void RenderingDeviceDriverWebGPU::fence_free(FenceID p_fence) {
	WGFence *fence = (WGFence *)(p_fence.id);
	if (!fence) {
		return;
	}

	// If an async work-done callback is in flight, mark freed and let
	// the callback handle deletion (use-after-free prevention).
	if (fence->work_done_pending) {
		fence->freed = true;
		return;
	}

	delete fence;
}

// =============================================================================
// SEMAPHORES (NO-OPS - single queue)
// =============================================================================

RDD::SemaphoreID RenderingDeviceDriverWebGPU::semaphore_create() {
	WGSemaphore *sem = new WGSemaphore();
	return SemaphoreID(sem);
}

void RenderingDeviceDriverWebGPU::semaphore_free(SemaphoreID p_semaphore) {
	WGSemaphore *sem = (WGSemaphore *)(p_semaphore.id);
	delete sem;
}

// =============================================================================
// COMMAND BUFFERS
// =============================================================================

RDD::CommandQueueFamilyID RenderingDeviceDriverWebGPU::command_queue_family_get(BitField<CommandQueueFamilyBits> p_cmd_queue_family_bits, RenderingContextDriver::SurfaceID p_surface) {
	return CommandQueueFamilyID(1); // Single family that supports everything.
}

RDD::CommandQueueID RenderingDeviceDriverWebGPU::command_queue_create(CommandQueueFamilyID p_cmd_queue_family, bool p_identify_as_main_queue) {
	WGCommandQueue *cq = new WGCommandQueue();
	cq->queue = queue; // Share the single device queue.
	return CommandQueueID(cq);
}

Error RenderingDeviceDriverWebGPU::command_queue_execute_and_present(CommandQueueID p_cmd_queue, VectorView<SemaphoreID> p_wait_semaphores, VectorView<CommandBufferID> p_cmd_buffers, VectorView<SemaphoreID> p_cmd_semaphores, FenceID p_cmd_fence, VectorView<SwapChainID> p_swap_chains) {
	// Submit all command buffers.
	LocalVector<WGPUCommandBuffer> wgpu_cmd_buffers;
	for (uint32_t i = 0; i < p_cmd_buffers.size(); i++) {
		WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffers[i].id);
		if (cmd && cmd->finished_buffer) {
			wgpu_cmd_buffers.push_back(cmd->finished_buffer);
		}
	}

	if (wgpu_cmd_buffers.size() > 0) {
		// Flush batched push constant data to GPU before submitting command buffers.
		if (push_constant_shadow_dirty_start < push_constant_shadow_dirty_end) {
			wgpuQueueWriteBuffer(queue, push_constant_ring_buffer, push_constant_shadow_dirty_start,
					push_constant_shadow + push_constant_shadow_dirty_start,
					push_constant_shadow_dirty_end - push_constant_shadow_dirty_start);
			push_constant_shadow_dirty_start = UINT32_MAX;
			push_constant_shadow_dirty_end = 0;
		}

		wgpuQueueSubmit(queue, wgpu_cmd_buffers.size(), wgpu_cmd_buffers.ptr());
		// Diagnostic: log submit count for the first few frames.
#ifdef WEBGPU_VERBOSE
		static int _submit_log = 0;
		if (_submit_log < 10) {
			EM_ASM({ console.log('[DIAG-SUBMIT] frame=' + $0 + ' cmds=' + $1); }, _submit_log, (int)wgpu_cmd_buffers.size());
			_submit_log++;
		}
#endif // WEBGPU_VERBOSE
	}

	// Signal fence when GPU work completes via async callback.
	if (p_cmd_fence) {
		WGFence *fence = (WGFence *)(p_cmd_fence.id);
		if (fence) {
			fence->signaled = false;
			fence->work_done_pending = true;
			WGPUQueueWorkDoneCallbackInfo cb = {};
			cb.mode = WGPUCallbackMode_AllowSpontaneous;
			cb.callback = _fence_work_done_callback;
			cb.userdata1 = fence;
			cb.userdata2 = nullptr;
			wgpuQueueOnSubmittedWorkDone(queue, cb);
		}
	}

	// Present swap chains.
	for (uint32_t i = 0; i < p_swap_chains.size(); i++) {
		WGSwapChain *sc = (WGSwapChain *)(p_swap_chains[i].id);
		if (sc && sc->surface) {
#ifndef __EMSCRIPTEN__
			wgpuSurfacePresent(sc->surface);
#endif
			// Post-submit diagnostic clear DISABLED — only pre-submit CYAN is active.
			// This lets us see if Godot's submit overwrites the cyan.

			// Release the surface texture and view so the browser can composite the
			// frame within this requestAnimationFrame callback. If we hold onto the
			// texture reference until next frame's acquire, the browser won't present
			// the rendered content.
			if (sc->current_view) {
				wgpuTextureViewRelease(sc->current_view);
				sc->current_view = nullptr;
			}
			if (sc->current_texture) {
				wgpuTextureRelease(sc->current_texture);
				sc->current_texture = nullptr;
			}
		}
	}

	// Clear finished command buffers (they are consumed by submit).
	for (uint32_t i = 0; i < p_cmd_buffers.size(); i++) {
		WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffers[i].id);
		if (cmd) {
			// Trigger async readback for any query pools that had timestamps resolved.
			for (uint32_t j = 0; j < cmd->written_query_pools.size(); j++) {
				WGQueryPool *pool = cmd->written_query_pools[j];
				if (pool->readback_pending && pool->readback_buffer) {
					// Only issue mapAsync if the buffer is actually in Unmapped state.
					// If a previous map is still pending (GPU hasn't completed), skip.
					WGPUBufferMapState state = wgpuBufferGetMapState(pool->readback_buffer);
					if (state != WGPUBufferMapState_Unmapped) {
						continue;
					}
					pool->map_generation++;
					WGPUBufferMapCallbackInfo cb_info = {};
					cb_info.mode = WGPUCallbackMode_AllowSpontaneous;
					cb_info.callback = _timestamp_readback_callback;
					cb_info.userdata1 = pool;
					cb_info.userdata2 = (void *)(uintptr_t)pool->map_generation;
					wgpuBufferMapAsync(pool->readback_buffer, WGPUMapMode_Read, 0, sizeof(uint64_t) * pool->count, cb_info);
				}
			}
			cmd->written_query_pools.clear();
			cmd->finished_buffer = nullptr;
		}
	}

	return OK;
}

void RenderingDeviceDriverWebGPU::command_queue_free(CommandQueueID p_cmd_queue) {
	WGCommandQueue *cq = (WGCommandQueue *)(p_cmd_queue.id);
	delete cq;
}

RDD::CommandPoolID RenderingDeviceDriverWebGPU::command_pool_create(CommandQueueFamilyID p_cmd_queue_family, CommandBufferType p_cmd_buffer_type) {
	WGCommandPool *pool = new WGCommandPool();
	pool->buffer_type = p_cmd_buffer_type;
	return CommandPoolID(pool);
}

bool RenderingDeviceDriverWebGPU::command_pool_reset(CommandPoolID p_cmd_pool) {
	// Nothing to reset — each command buffer creates its own encoder.
	return true;
}

void RenderingDeviceDriverWebGPU::command_pool_free(CommandPoolID p_cmd_pool) {
	WGCommandPool *pool = (WGCommandPool *)(p_cmd_pool.id);
	delete pool;
}

RDD::CommandBufferID RenderingDeviceDriverWebGPU::command_buffer_create(CommandPoolID p_cmd_pool) {
	WGCommandBuffer *cmd = new WGCommandBuffer();
	return CommandBufferID(cmd);
}

bool RenderingDeviceDriverWebGPU::command_buffer_begin(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL_V(cmd, false);

	WGPUCommandEncoderDescriptor desc = {};
	cmd->encoder = wgpuDeviceCreateCommandEncoder(device, &desc);
	ERR_FAIL_COND_V(cmd->encoder == nullptr, false);

	cmd->active_encoder = WGCommandBuffer::NONE;
	cmd->push_constants_dirty = false;
	cmd->push_constant_data_len = 0;

	return true;
}

bool RenderingDeviceDriverWebGPU::command_buffer_begin_secondary(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, uint32_t p_subpass, FramebufferID p_framebuffer) {
	// WebGPU has no secondary command buffers. Treat as primary.
	return command_buffer_begin(p_cmd_buffer);
}

void RenderingDeviceDriverWebGPU::command_buffer_end(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// End any active render/compute pass.
	cmd->end_active_encoder();

	// Resolve any query pools that had timestamps written during this command buffer.
	for (uint32_t i = 0; i < cmd->written_query_pools.size(); i++) {
		WGQueryPool *pool = cmd->written_query_pools[i];
		if (!pool->readback_buffer) {
			continue;
		}

		// Ensure the readback buffer is in Unmapped state before encoding a copy to it.
		// The previous frame's mapAsync may still be pending if the GPU hasn't completed.
		// emdawnwebgpu does not reliably cancel pending maps via wgpuBufferUnmap, so we
		// check the actual state and skip this frame's resolve+copy if the buffer is stuck.
		WGPUBufferMapState map_state = wgpuBufferGetMapState(pool->readback_buffer);
		if (map_state != WGPUBufferMapState_Unmapped) {
			// Try to drain pending callbacks first.
			WGPUInstance inst = context_driver ? context_driver->get_instance() : nullptr;
			if (inst) {
				wgpuInstanceProcessEvents(inst);
			}
			map_state = wgpuBufferGetMapState(pool->readback_buffer);

			if (map_state != WGPUBufferMapState_Unmapped) {
				wgpuBufferUnmap(pool->readback_buffer);
				if (inst) {
					wgpuInstanceProcessEvents(inst);
				}
				map_state = wgpuBufferGetMapState(pool->readback_buffer);
			}

			if (map_state != WGPUBufferMapState_Unmapped) {
				// Buffer is permanently stuck — skip resolve+copy to avoid validation error.
				pool->readback_pending = false;
				continue;
			}
		}
		pool->readback_pending = false;

		wgpuCommandEncoderResolveQuerySet(cmd->encoder, pool->handle, 0, pool->count, pool->resolve_buffer, 0);
		uint64_t byte_size = sizeof(uint64_t) * pool->count;
		wgpuCommandEncoderCopyBufferToBuffer(cmd->encoder, pool->resolve_buffer, 0, pool->readback_buffer, 0, byte_size);
		pool->readback_pending = true;
	}

	// Finish the command encoder.
	if (cmd->encoder) {
		cmd->finished_buffer = wgpuCommandEncoderFinish(cmd->encoder, nullptr);
		wgpuCommandEncoderRelease(cmd->encoder);
		cmd->encoder = nullptr;
	}
}

void RenderingDeviceDriverWebGPU::command_buffer_execute_secondary(CommandBufferID p_cmd_buffer, VectorView<CommandBufferID> p_secondary_cmd_buffers) {
	// No-op: WebGPU has no secondary command buffers.
}

// =============================================================================
// SWAP CHAIN
// =============================================================================

RDD::SwapChainID RenderingDeviceDriverWebGPU::swap_chain_create(RenderingContextDriver::SurfaceID p_surface) {
	WGSwapChain *sc = new WGSwapChain();
	sc->surface = context_driver->surface_get_handle(p_surface);
	sc->surface_id = p_surface;
	sc->format = WGPUTextureFormat_BGRA8Unorm; // Fallback if capabilities can't be queried.

	// Query the browser's actual preferred canvas format (Task 7.11) instead of
	// assuming BGRA8Unorm -- Chrome/ANGLE-Vulkan prefers BGRA8Unorm, but other
	// backends (e.g. some Linux/Vulkan configurations) report RGBA8Unorm as
	// preferred instead. Configuring the surface with a non-preferred format
	// forces Dawn to insert an extra blit/copy every frame to compensate (the
	// "WebGPU canvas configured with a different format than is preferred by
	// this device" console warning) -- purely a perf cost, not a correctness
	// one, but a real, measurable one. emdawnwebgpu's wgpuSurfaceGetCapabilities()
	// always returns the true preferred format first (see its kSurfaceFormatsRGBAFirst/
	// kSurfaceFormatsBGRAFirst tables), so formats[0] is exactly what we want.
	WGPUSurfaceCapabilities caps = {};
	if (wgpuSurfaceGetCapabilities(sc->surface, context_driver->get_adapter(), &caps) == WGPUStatus_Success && caps.formatCount > 0) {
		sc->format = caps.formats[0];
		wgpuSurfaceCapabilitiesFreeMembers(caps);
	}

	// Create a render pass descriptor for this swap chain.
	// Used by swap_chain_get_render_pass() so the RD layer can create compatible pipelines.
	WGRenderPass *rp = new WGRenderPass();
	RDD::Attachment att;
	att.format = _wgpu_to_data_format(sc->format);
	att.samples = TEXTURE_SAMPLES_1;
	att.load_op = ATTACHMENT_LOAD_OP_CLEAR;
	att.store_op = ATTACHMENT_STORE_OP_STORE;
	att.stencil_load_op = ATTACHMENT_LOAD_OP_DONT_CARE;
	att.stencil_store_op = ATTACHMENT_STORE_OP_DONT_CARE;
	att.initial_layout = TEXTURE_LAYOUT_UNDEFINED;
	att.final_layout = TEXTURE_LAYOUT_UNDEFINED;
	rp->attachments.push_back(att);

	WGRenderPass::SubpassInfo subpass;
	RDD::AttachmentReference color_ref;
	color_ref.attachment = 0;
	color_ref.layout = TEXTURE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	subpass.color_references.push_back(color_ref);
	rp->subpasses.push_back(subpass);
	rp->is_swap_chain_pass = true;
	sc->render_pass = rp;

	return SwapChainID(sc);
}

Error RenderingDeviceDriverWebGPU::swap_chain_resize(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, uint32_t p_desired_framebuffer_count) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(sc->surface == nullptr, ERR_INVALID_PARAMETER);

	uint32_t width = context_driver->surface_get_width(sc->surface_id);
	uint32_t height = context_driver->surface_get_height(sc->surface_id);
	if (width == 0 || height == 0) {
		return ERR_SKIP; // Canvas not yet sized.
	}

	// If previously configured, unconfigure first to reconfigure with new dimensions.
	if (sc->configured) {
		wgpuSurfaceUnconfigure(sc->surface);
		sc->configured = false;
	}

	WGPUSurfaceConfiguration config = {};
	config.device = device;
	config.format = sc->format;
	config.usage = WGPUTextureUsage_RenderAttachment;
	config.alphaMode = WGPUCompositeAlphaMode_Opaque;
	config.presentMode = WGPUPresentMode_Fifo; // Browser always vsyncs via requestAnimationFrame.
	config.width = width;
	config.height = height;
	wgpuSurfaceConfigure(sc->surface, &config);

	// Uncaptured error listener + monkey patches are installed in initialize().

	sc->width = width;
	sc->height = height;
	sc->configured = true;
	// Clear the needs_resize flag so swap_chain_acquire_framebuffer doesn't
	context_driver->surface_set_needs_resize(sc->surface_id, false);
	return OK;
}

RDD::FramebufferID RenderingDeviceDriverWebGPU::swap_chain_acquire_framebuffer(CommandQueueID p_cmd_queue, SwapChainID p_swap_chain, bool &r_resize_required) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, FramebufferID());
	if (!sc->configured || context_driver->surface_get_needs_resize(sc->surface_id)) {
		// Not yet sized or surface dimensions changed — request a resize.
		if (!sc->configured) {
			print_verbose("WebGPU: swap_chain_acquire_framebuffer: not configured, requesting resize");
		}
		r_resize_required = true;
		return FramebufferID();
	}

	// Release resources from the previous frame.
	if (sc->current_framebuffer) {
		delete sc->current_framebuffer;
		sc->current_framebuffer = nullptr;
	}
	if (sc->current_view) {
		wgpuTextureViewRelease(sc->current_view);
		sc->current_view = nullptr;
	}
	if (sc->current_texture) {
		wgpuTextureRelease(sc->current_texture);
		sc->current_texture = nullptr;
	}

	// Acquire the next swap chain texture.
	WGPUSurfaceTexture surface_texture = {};
	wgpuSurfaceGetCurrentTexture(sc->surface, &surface_texture);

	// Diagnostic: log surface texture status for the first few frames.
	static int _st_log = 0;
	if (_st_log < 10) {
		WEBGPU_DIAG({ console.log('[SURFACE] status=' + $0 + ' texture=' + ($1 ? 'valid' : 'NULL')); }, (int)surface_texture.status, (int)(surface_texture.texture != nullptr));
		_st_log++;
	}

	if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Lost) {
		ERR_PRINT_ONCE("WebGPU: surface lost — device or surface is no longer usable.");
		return FramebufferID();
	}

	if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal ||
			surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated) {
		WARN_PRINT_ONCE(vformat("WebGPU: wgpuSurfaceGetCurrentTexture: resize-needed status=%d", (int)surface_texture.status));
		r_resize_required = true;
		return FramebufferID();
	}
	if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) {
		WARN_PRINT_ONCE(vformat("WebGPU: wgpuSurfaceGetCurrentTexture: unexpected status=%d", (int)surface_texture.status));
		return FramebufferID();
	}

	sc->current_texture = surface_texture.texture;

	// Get actual surface texture dimensions. On web, wgpuSurfaceConfigure may be called
	// with OS physical-pixel dimensions (e.g. 1152×648 on HiDPI), but the browser canvas
	// may be CSS-sized (e.g. 756×417). wgpuTextureGetWidth gives the real GPU texture size.
	uint32_t actual_w = wgpuTextureGetWidth(sc->current_texture);
	uint32_t actual_h = wgpuTextureGetHeight(sc->current_texture);
	if (actual_w != sc->width || actual_h != sc->height) {
		WARN_PRINT_ONCE(vformat("WebGPU: surface texture (%d x %d) differs from configured (%d x %d) — using actual size",
				actual_w, actual_h, sc->width, sc->height));
		sc->width = actual_w;
		sc->height = actual_h;
		// Sync the context driver's stored surface dimensions so that screen_get_width/height
		// (used by the blit dst_rect normalization) returns the actual canvas size, not the
		// configured HiDPI physical-pixel size. Without this, the blit only covers a fraction
		// of the swap chain surface.
		context_driver->surface_set_size(sc->surface_id, actual_w, actual_h);
	}

	// Create a view for this frame's swap chain texture.
	WGPUTextureViewDescriptor view_desc = {};
	view_desc.format = sc->format;
	view_desc.dimension = WGPUTextureViewDimension_2D;
	view_desc.baseMipLevel = 0;
	view_desc.mipLevelCount = 1;
	view_desc.baseArrayLayer = 0;
	view_desc.arrayLayerCount = 1;
	view_desc.aspect = WGPUTextureAspect_All;
	sc->current_view = wgpuTextureCreateView(sc->current_texture, &view_desc);
	ERR_FAIL_COND_V(sc->current_view == nullptr, FramebufferID());

	// Wrap in a WGFramebuffer. The WGTexture pointer is null since we manage
	// the texture lifetime through the swap chain, not the framebuffer.
	WGFramebuffer *fb = new WGFramebuffer();
	fb->render_pass = sc->render_pass;
	fb->width = sc->width;
	fb->height = sc->height;
	fb->attachments.push_back(nullptr);
	fb->attachment_views.push_back(sc->current_view);
	sc->current_framebuffer = fb;

	r_resize_required = false;
	return FramebufferID(fb);
}

RDD::RenderPassID RenderingDeviceDriverWebGPU::swap_chain_get_render_pass(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, RenderPassID());
	return RenderPassID(sc->render_pass);
}

RDD::DataFormat RenderingDeviceDriverWebGPU::swap_chain_get_format(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL_V(sc, DATA_FORMAT_MAX);
	return _wgpu_to_data_format(sc->format);
}

RDD::ColorSpace RenderingDeviceDriverWebGPU::swap_chain_get_color_space(SwapChainID p_swap_chain) {
	// Browser canvas surfaces are always sRGB.
	return COLOR_SPACE_REC709_NONLINEAR_SRGB;
}

bool RenderingDeviceDriverWebGPU::swap_chain_get_hdr_output_supported(SwapChainID p_swap_chain) {
	return false; // Not supported by this driver.
}

void RenderingDeviceDriverWebGPU::swap_chain_free(SwapChainID p_swap_chain) {
	WGSwapChain *sc = (WGSwapChain *)(p_swap_chain.id);
	ERR_FAIL_NULL(sc);
	if (sc->current_framebuffer) {
		delete sc->current_framebuffer;
	}
	if (sc->current_view) {
		wgpuTextureViewRelease(sc->current_view);
	}
	if (sc->current_texture) {
		wgpuTextureRelease(sc->current_texture);
	}
	if (sc->render_pass) {
		delete sc->render_pass;
	}
	if (sc->surface && sc->configured) {
		wgpuSurfaceUnconfigure(sc->surface);
	}
	delete sc;
}

// =============================================================================
// FRAMEBUFFER
// =============================================================================

RDD::FramebufferID RenderingDeviceDriverWebGPU::framebuffer_create(RenderPassID p_render_pass, VectorView<TextureID> p_attachments, uint32_t p_width, uint32_t p_height) {
	WGFramebuffer *fb = new WGFramebuffer();
	fb->render_pass = (WGRenderPass *)(p_render_pass.id);
	fb->width = p_width;
	fb->height = p_height;

	for (uint32_t i = 0; i < p_attachments.size(); i++) {
		WGTexture *tex = (WGTexture *)(p_attachments[i].id);
		fb->attachments.push_back(tex);
		fb->attachment_views.push_back(tex ? tex->default_view : nullptr);
	}

	return FramebufferID(fb);
}

void RenderingDeviceDriverWebGPU::framebuffer_free(FramebufferID p_framebuffer) {
	WGFramebuffer *fb = (WGFramebuffer *)(p_framebuffer.id);
	delete fb;
}

// =============================================================================
// SHADER
// =============================================================================

// Helper: map Godot stage mask bits to WGPUShaderStage visibility flags.
// After Tint processing (comparison splitting, type changes), the WGSL shader
// may reference bindings in stages not predicted by the original SPIR-V reflection.
// To avoid visibility mismatches, OR in both Vertex and Fragment for any render
// shader that uses either stage. Compute stays separate.
static WGPUShaderStage _stages_to_wgpu_visibility(uint32_t p_stage_mask) {
	WGPUShaderStage vis = WGPUShaderStage_None;
	bool has_render = (p_stage_mask & ((1u << RDD::SHADER_STAGE_VERTEX) | (1u << RDD::SHADER_STAGE_FRAGMENT) | (1u << RDD::SHADER_STAGE_TESSELATION_CONTROL) | (1u << RDD::SHADER_STAGE_TESSELATION_EVALUATION))) != 0;
	if (has_render) {
		vis = (WGPUShaderStage)(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
	}
	if (p_stage_mask & (1u << RDD::SHADER_STAGE_COMPUTE)) {
		vis = (WGPUShaderStage)(vis | WGPUShaderStage_Compute);
	}
	return vis;
}

static bool _is_ident_char(char p_c) {
	return (p_c >= 'a' && p_c <= 'z') || (p_c >= 'A' && p_c <= 'Z') || (p_c >= '0' && p_c <= '9') || p_c == '_';
}

// Find the index of the ')' that matches the '(' at p_open_paren (which must
// point at a '(' character). Returns -1 if unbalanced.
static int64_t _find_matching_paren(const char *p_str, int64_t p_len, int64_t p_open_paren) {
	int depth = 0;
	for (int64_t i = p_open_paren; i < p_len; i++) {
		if (p_str[i] == '(') {
			depth++;
		} else if (p_str[i] == ')') {
			depth--;
			if (depth == 0) {
				return i;
			}
		}
	}
	return -1;
}

// Finds the [start, end) byte range of the p_index'th (0-based) top-level
// comma-separated argument within [p_args_start, p_args_end) (the region
// strictly inside a call's parens). Sets r_start to -1 if there aren't
// enough top-level arguments.
static void _find_nth_top_level_arg(const char *p_str, int64_t p_args_start, int64_t p_args_end, int p_index, int64_t &r_start, int64_t &r_end) {
	int depth = 0;
	int64_t arg_start = p_args_start;
	int arg_idx = 0;
	for (int64_t i = p_args_start; i < p_args_end; i++) {
		char c = p_str[i];
		if (c == '(' || c == '[') {
			depth++;
		} else if (c == ')' || c == ']') {
			depth--;
		} else if (c == ',' && depth == 0) {
			if (arg_idx == p_index) {
				r_start = arg_start;
				r_end = i;
				return;
			}
			arg_idx++;
			arg_start = i + 1;
		}
	}
	if (arg_idx == p_index) {
		r_start = arg_start;
		r_end = p_args_end;
		return;
	}
	r_start = -1;
	r_end = -1;
}

// Detects texture_2d<f32> combined-binding variables that are ONLY EVER read
// via a single-component swizzle (.x or .r) following a non-comparison
// sample/load call, and rewrites the declaration + every such call site to
// texture_depth_2d (dropping the swizzle, since texture_depth_2d's sample/
// load functions already return a plain f32, not a vec4).
//
// Why this is needed: a binding like Bokeh DOF's `source_depth`
// (servers/rendering/renderer_rd/shaders/effects/bokeh_dof_raster.glsl) is
// genuinely fed a real WebGPU depth-format texture by its C++ caller
// (BokehDOF::bokeh_dof_raster() binds the scene's actual depth buffer), but
// Tint compiles it as texture_2d<f32> because GLSL's plain `sampler2D` /
// `texture()` / `textureLod()` give it no way to signal "this samples a
// depth-format resource" distinctly from an ordinary float texture --
// glslang emits identical (Depth=0) SPIR-V either way. With no such signal,
// the driver's existing Depth/Float mismatch handling (see the
// _is_depth_format() fallback substitution further below) can only assume
// the binding truly wants Float and silently substitutes a meaningless 4x4
// dummy texture for the real depth data -- producing plausible-looking but
// numerically wrong output (e.g. Bokeh DOF's blur amount/falloff looking
// wrong while still blurring *something*). See webgpu_notes/TASKS.md Task
// 7.13.
//
// This is conservatively scoped: a texture is only rewritten if EVERY
// occurrence of its identifier in the whole module is either the
// declaration itself, the first argument of textureSample/
// textureSampleLevel/textureSampleBias/textureSampleGrad/textureLoad
// immediately followed by .x or .r, or the 2nd argument of textureGather(
// component, tex, samp, coords) (dropping the leading component argument
// entirely instead, since texture_depth_2d's textureGather overload has no
// component parameter at all -- see Task 7.22) -- any other usage (full
// vec4 access, .rgb/.xy, textureSampleCompare*, being passed to a
// user-defined function, textureDimensions, etc.) disqualifies it and
// leaves the original Float-typed binding untouched. Once rewritten, this
// binding's BindGroupLayout entry naturally ends up Depth-sampleType via
// the existing wgsl_is_depth_texture-driven scan just below in
// shader_create_from_container() (which runs against this function's
// output), so the real depth texture gets bound directly -- no fallback
// needed. Every draw using this compiled shader module must supply a real
// depth-format texture at this binding from then on; verified true by
// construction for Bokeh DOF (bokeh_dof.cpp has exactly one call site that
// creates this uniform, always from the scene's real depth buffer).
// Finds the "@group(G) @binding(B)" attribute pair immediately preceding the
// declaration whose identifier starts at p_name_start (bounded by the previous
// ';'/'}' so an unrelated, earlier declaration's attributes are never picked
// up), and parses it via parse_group_binding(). Returns false if none found.
static bool _find_preceding_group_binding(const char *p_wgsl, int64_t p_name_start, unsigned int &r_grp, unsigned int &r_bnd) {
	int64_t stmt_start = 0;
	for (int64_t j = p_name_start - 1; j >= 0; j--) {
		if (p_wgsl[j] == ';' || p_wgsl[j] == '}') {
			stmt_start = j + 1;
			break;
		}
	}
	const char *best = nullptr;
	const char *scan = p_wgsl + stmt_start;
	while (true) {
		const char *found = strstr(scan, "@group(");
		if (!found || (found - p_wgsl) >= p_name_start) {
			break;
		}
		best = found;
		scan = found + 7;
	}
	if (!best) {
		return false;
	}
	return parse_group_binding(best, r_grp, r_bnd);
}

// p_storage_converted_keys: (group<<16 | binding) keys that were just converted
// from a readonly storage texture to a sampled texture_2d<f32> by the
// read-only-storage-to-sampled pass (see wgsl_read_storage_to_sampled just
// above shader_create_from_container()'s call site). These are NEVER real
// depth-format textures -- they're ordinary Godot color-format storage
// textures (e.g. voxel_gi.glsl's r32f "depth"/"source_depth" dynamic-GI
// light-injection buffers, which store a plain distance/height value, not a
// hardware depth attachment) that merely happen to be named "depth" and read
// back one component at a time, which is exactly this function's structural
// heuristic. Reclassifying them to texture_depth_2d produces a WGSL module
// that declares Depth sampleType while the BindGroupLayout (built from the
// original storage format, e.g. R32Float -> UnfilterableFloat) still expects
// Float/UnfilterableFloat, a hard Dawn validation error at pipeline-creation
// time. Must be excluded before the naming heuristic below ever gets a
// chance to misfire on them. See webgpu_notes/TASKS.md Task 9.5 Round 5.
static char *_reclassify_single_component_depth_textures(char *p_wgsl_str, const HashSet<uint32_t> &p_storage_converted_keys) {
	static const char *const TEX_TYPE = "texture_2d<f32>";
	static const int TEX_TYPE_LEN = 15;
	// textureSampleBias/textureSampleGrad are NOT defined for texture_depth_2d
	// in WGSL at all (depth textures don't support mip bias/gradient
	// sampling) -- deliberately excluded here so any texture used via either
	// disqualifies (falls through to "no matching call prefix" below).
	static const char *const CALL_FNS[] = {
		"textureSampleLevel(",
		"textureSample(",
		"textureLoad(",
	};
	static const int CALL_FN_COUNT = 3;

	const char *wgsl = p_wgsl_str;
	const int64_t len = (int64_t)strlen(wgsl);

	struct Candidate {
		String name;
		int64_t name_start = 0;
		int64_t type_start = 0;
	};
	Vector<Candidate> candidates;
	{
		const char *scan = wgsl;
		while (true) {
			const char *found = strstr(scan, TEX_TYPE);
			if (!found) {
				break;
			}
			int64_t type_pos = found - wgsl;
			int64_t i = type_pos - 1;
			while (i >= 0 && (wgsl[i] == ' ' || wgsl[i] == '\t')) {
				i--;
			}
			if (i >= 0 && wgsl[i] == ':') {
				i--;
				while (i >= 0 && (wgsl[i] == ' ' || wgsl[i] == '\t')) {
					i--;
				}
				int64_t name_end = i + 1;
				while (i >= 0 && _is_ident_char(wgsl[i])) {
					i--;
				}
				int64_t name_start = i + 1;
				if (name_start < name_end) {
					Candidate c;
					c.name = String::utf8(wgsl + name_start, (int)(name_end - name_start));
					c.name_start = name_start;
					c.type_start = type_pos;
					candidates.push_back(c);
				}
			}
			scan = found + TEX_TYPE_LEN;
		}
	}
	if (candidates.is_empty()) {
		return p_wgsl_str;
	}

	struct Edit {
		int64_t start = 0;
		int64_t end = 0; // [start, end) in the ORIGINAL text.
		String replacement;
	};
	Vector<Edit> edits;

	for (const Candidate &c : candidates) {
		CharString name_cs = c.name.utf8();
		const char *name = name_cs.get_data();
		const int name_len = name_cs.length();
		if (name_len == 0) {
			continue;
		}

		if (!p_storage_converted_keys.is_empty()) {
			unsigned int grp = 0, bnd = 0;
			if (_find_preceding_group_binding(wgsl, c.name_start, grp, bnd)) {
				uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
				if (p_storage_converted_keys.has(key)) {
					continue;
				}
			}
		}

		// The single-component-access structural heuristic alone is NOT
		// sufficient: plenty of legitimate texture_2d<f32> bindings are
		// single-channel by nature without being depth-format data (e.g.
		// CanvasSdfShaderRD's SDF distance texture, sampled and used the
		// exact same .x-only way). Require the WGSL variable name to also
		// contain "depth" (case-insensitive) -- Godot's own GLSL source
		// consistently names depth-buffer-as-texture uniforms this way
		// (source_depth, depth_tex, depth_buffer, ...) -- as a second,
		// independent signal before touching a binding. This is
		// conservative by design: shaders whose depth-reading uniform
		// happens not to follow this naming convention will keep hitting
		// the pre-existing dummy-texture fallback instead of being fixed,
		// which is no worse than the status quo, whereas a false positive
		// here breaks a previously-working, unrelated shader outright.
		{
			bool has_depth_in_name = false;
			for (int ni = 0; ni + 5 <= name_len; ni++) {
				if ((name[ni] == 'd' || name[ni] == 'D') &&
						(name[ni + 1] == 'e' || name[ni + 1] == 'E') &&
						(name[ni + 2] == 'p' || name[ni + 2] == 'P') &&
						(name[ni + 3] == 't' || name[ni + 3] == 'T') &&
						(name[ni + 4] == 'h' || name[ni + 4] == 'H')) {
					has_depth_in_name = true;
					break;
				}
			}
			if (!has_depth_in_name) {
				continue;
			}

			// Godot's own SSR code (screen_space_reflection_resolve.glsl's
			// "source_depth_half", bound from RB_HIZ -- a plain R32Float linear-
			// depth derivative, never a real depth-attachment texture, see
			// ss_effects.cpp's ssr_resolve()) uses "_half" specifically to
			// distinguish a downsampled/derived depth buffer from the real one
			// ("source_depth", bound from get_depth_texture(), on the very same
			// shader). The name+single-component heuristic alone can't tell
			// these apart -- both are named with "depth" and read single-
			// component -- so treat "half" anywhere in the name as a third,
			// independent disqualifying signal, mirroring how the storage-
			// converted-keys exclusion set already handles the other two known
			// synthetic-binding false-positive sources. A real depth-format
			// binding legitimately named with "half" in it would be highly
			// unusual and is not known to exist in this engine's shaders.
			bool has_half_in_name = false;
			for (int ni = 0; ni + 4 <= name_len; ni++) {
				if ((name[ni] == 'h' || name[ni] == 'H') &&
						(name[ni + 1] == 'a' || name[ni + 1] == 'A') &&
						(name[ni + 2] == 'l' || name[ni + 2] == 'L') &&
						(name[ni + 3] == 'f' || name[ni + 3] == 'F')) {
					has_half_in_name = true;
					break;
				}
			}
			if (has_half_in_name) {
				continue;
			}

			// AMD FidelityFX FSR2's `r_dilatedDepth` (ffx_fsr2_callbacks_glsl.h,
			// read via `texelFetch(r_dilatedDepth, ...).r` -- single-component,
			// exactly this heuristic's structural trigger) is the same false-
			// positive shape again: an ordinary r16f-format buffer FSR2 itself
			// computes and writes (`rw_dilatedDepth`, StoreDilatedDepth), never a
			// real depth-attachment texture, that merely has "Depth" in its name
			// because it holds a dilated *copy* of depth values. Every "dilated"-
			// prefixed FFX resource (DilatedDepth/DilatedMotionVectors/
			// DilatedReactiveMasks) is one of these synthetic intermediate
			// buffers by construction, never the real hardware depth buffer, so
			// "dilated" anywhere in the name is a fourth independent
			// disqualifying signal, mirroring "half" above. Found live: Dawn
			// rejected FSR2's depth-clip pass bind group with "None of the
			// supported sample types (Float|UnfilterableFloat) ... match the
			// expected sample types (Depth)" the first time FSR2 actually ran on
			// WebGPU. See webgpu_notes/TASKS.md's FSR2 task.
			bool has_dilated_in_name = false;
			for (int ni = 0; ni + 7 <= name_len; ni++) {
				if ((name[ni] == 'd' || name[ni] == 'D') &&
						(name[ni + 1] == 'i' || name[ni + 1] == 'I') &&
						(name[ni + 2] == 'l' || name[ni + 2] == 'L') &&
						(name[ni + 3] == 'a' || name[ni + 3] == 'A') &&
						(name[ni + 4] == 't' || name[ni + 4] == 'T') &&
						(name[ni + 5] == 'e' || name[ni + 5] == 'E') &&
						(name[ni + 6] == 'd' || name[ni + 6] == 'D')) {
					has_dilated_in_name = true;
					break;
				}
			}
			if (has_dilated_in_name) {
				continue;
			}
		}

		bool disqualified = false;
		Vector<Edit> call_edits;

		const char *p = wgsl;
		while (true) {
			const char *found = strstr(p, name);
			if (!found) {
				break;
			}
			int64_t pos = found - wgsl;
			p = found + 1;

			bool left_ok = (pos == 0) || !_is_ident_char(wgsl[pos - 1]);
			int64_t after = pos + name_len;
			bool right_ok = (after >= len) || !_is_ident_char(wgsl[after]);
			if (!left_ok || !right_ok) {
				continue;
			}

			if (pos == c.name_start) {
				continue; // The declaration's own identifier -- fine.
			}

			bool matched_call = false;
			for (int fi = 0; fi < CALL_FN_COUNT; fi++) {
				const char *fn = CALL_FNS[fi];
				size_t fn_len = strlen(fn);
				int64_t probe = pos;
				while (probe > 0 && (wgsl[probe - 1] == ' ' || wgsl[probe - 1] == '\t')) {
					probe--;
				}
				int64_t fn_start = probe - (int64_t)fn_len;
				if (fn_start < 0 || strncmp(wgsl + fn_start, fn, fn_len) != 0) {
					continue;
				}
				int64_t open_paren = fn_start + (int64_t)fn_len - 1;
				int64_t close_paren = _find_matching_paren(wgsl, len, open_paren);
				if (close_paren < 0) {
					continue;
				}
				int64_t after_call = close_paren + 1;
				if (after_call + 1 < len && wgsl[after_call] == '.' &&
						(wgsl[after_call + 1] == 'x' || wgsl[after_call + 1] == 'r')) {
					int64_t after_swizzle = after_call + 2;
					bool swizzle_ok = (after_swizzle >= len) || !_is_ident_char(wgsl[after_swizzle]);
					if (swizzle_ok) {
						// texture_depth_2d's textureSampleLevel takes an i32
						// level (unlike texture_2d<f32>'s f32 level) -- wrap
						// the 4th argument (0-indexed: 3) in i32(...) so a
						// float-literal/expression level still type-checks.
						bool level_wrap_ok = true;
						Edit level_edit;
						if (strcmp(fn, "textureSampleLevel(") == 0) {
							int64_t a_start, a_end;
							_find_nth_top_level_arg(wgsl, open_paren + 1, close_paren, 3, a_start, a_end);
							if (a_start < 0) {
								level_wrap_ok = false;
							} else {
								int64_t trimmed_start = a_start;
								int64_t trimmed_end = a_end;
								while (trimmed_start < trimmed_end && (wgsl[trimmed_start] == ' ' || wgsl[trimmed_start] == '\t')) {
									trimmed_start++;
								}
								while (trimmed_end > trimmed_start && (wgsl[trimmed_end - 1] == ' ' || wgsl[trimmed_end - 1] == '\t')) {
									trimmed_end--;
								}
								level_edit.start = a_start;
								level_edit.end = a_end;
								level_edit.replacement = "i32(" + String::utf8(wgsl + trimmed_start, (int)(trimmed_end - trimmed_start)) + ")";
							}
						}
						if (level_wrap_ok) {
							Edit e;
							e.start = after_call;
							e.end = after_call + 2;
							e.replacement = "";
							call_edits.push_back(e);
							if (strcmp(fn, "textureSampleLevel(") == 0) {
								call_edits.push_back(level_edit);
							}
							matched_call = true;
							break;
						}
					}
				}
				break; // Function-prefix matched but a disqualifying usage shape.
			}
			if (!matched_call) {
				// texture_depth_2d's textureGather overload drops the leading
				// component argument entirely (textureGather(tex, samp, coords)),
				// unlike texture_2d<f32>'s (textureGather(component, tex, samp,
				// coords)) -- GLSL's implicit-default-component textureGather(
				// sampler, uv) always converts to WGSL's explicit `textureGather(
				// 0i, tex, samp, uv)` form, so this isn't a swizzle-drop like the
				// CALL_FNS cases above but an argument-drop. See Task 7.22.
				static const char *const GATHER_FN = "textureGather(";
				static const int64_t GATHER_FN_LEN = 14;
				const char *gscan = wgsl;
				while (true) {
					const char *gfound = strstr(gscan, GATHER_FN);
					if (!gfound) {
						break;
					}
					int64_t g_open = (gfound - wgsl) + GATHER_FN_LEN - 1;
					int64_t g_close = _find_matching_paren(wgsl, len, g_open);
					gscan = gfound + 1;
					if (g_close < 0 || g_open >= pos || g_close <= pos) {
						continue; // This occurrence of `name` isn't inside this call's parens.
					}
					int64_t a1_start, a1_end;
					_find_nth_top_level_arg(wgsl, g_open + 1, g_close, 1, a1_start, a1_end);
					if (a1_start < 0) {
						continue;
					}
					int64_t t_start = a1_start, t_end = a1_end;
					while (t_start < t_end && (wgsl[t_start] == ' ' || wgsl[t_start] == '\t')) {
						t_start++;
					}
					while (t_end > t_start && (wgsl[t_end - 1] == ' ' || wgsl[t_end - 1] == '\t')) {
						t_end--;
					}
					if (t_start != pos || t_end != pos + name_len) {
						continue; // `name` isn't (whitespace aside) exactly the 2nd argument.
					}
					int64_t a0_start, a0_end;
					_find_nth_top_level_arg(wgsl, g_open + 1, g_close, 0, a0_start, a0_end);
					if (a0_start < 0) {
						continue;
					}
					Edit e;
					e.start = a0_start;
					e.end = t_start; // Drop the component arg plus its trailing comma/whitespace.
					e.replacement = "";
					call_edits.push_back(e);
					matched_call = true;
					break;
				}
			}
			if (!matched_call) {
				disqualified = true;
				break;
			}
		}

		if (disqualified || call_edits.is_empty()) {
			continue;
		}

		Edit decl_edit;
		decl_edit.start = c.type_start;
		decl_edit.end = c.type_start + TEX_TYPE_LEN;
		decl_edit.replacement = "texture_depth_2d";
		edits.push_back(decl_edit);
		for (const Edit &ce : call_edits) {
			edits.push_back(ce);
		}
	}

	if (edits.is_empty()) {
		return p_wgsl_str;
	}

	// Small insertion sort by start offset (edit counts per shader are tiny).
	for (int i = 1; i < edits.size(); i++) {
		Edit key = edits[i];
		int j = i - 1;
		while (j >= 0 && edits[j].start > key.start) {
			edits.set(j + 1, edits[j]);
			j--;
		}
		edits.set(j + 1, key);
	}

	String out;
	int64_t cursor = 0;
	for (const Edit &e : edits) {
		if (e.start < cursor) {
			continue; // Overlap safety net; shouldn't happen given disjoint sources.
		}
		out += String::utf8(wgsl + cursor, (int)(e.start - cursor));
		out += e.replacement;
		cursor = e.end;
	}
	out += String::utf8(wgsl + cursor, (int)(len - cursor));

	free(p_wgsl_str);
	CharString cs = out.utf8();
	char *result = (char *)malloc(cs.length() + 1);
	memcpy(result, cs.get_data(), cs.length() + 1);
	return result;
}

// Rewrites WGSL storage-texel-format names that WebGPU doesn't support to
// supported equivalents, and demotes vertex-stage read_write storage buffers
// to read-only (WebGPU forbids a writable storage buffer's visibility from
// including the vertex stage at all -- see the fragment-stage note below).
// Shared between shader_create_from_container() (the primary conversion path)
// and _create_module_with_spec_constants() (the legacy per-specialization
// re-conversion path) -- previously hand-duplicated between the two behind a
// "must match shader_create_from_container" comment, which is exactly the
// kind of thing that silently drifts; see webgpu_notes/TASKS.md's plan-of-attack
// notes (Tier 2 "duplicated format remapping") for context.
void RenderingDeviceDriverWebGPU::_remap_unsupported_wgsl_storage_formats(char *&r_wgsl_str, ShaderStage p_stage) const {
	// p_stage is no longer used here (vertex-stage read_write demotion moved to
	// the Tint IR level -- see the NOTE at the bottom of this function); kept in
	// the signature since removing it would require touching both call sites for
	// no benefit, and a future format remap may again need to be stage-specific.
	(void)p_stage;
	// Remap unsupported 8-bit storage texture format names in WGSL.
	// r8* and rg8* are not valid base WebGPU storage texel formats — remap to
	// 32-bit equivalents. With texture-formats-tier1 these formats are valid
	// natively, so skip the remap to preserve blendable/filterable properties.
	if (!has_texture_formats_tier1 &&
			(strstr(r_wgsl_str, "r8unorm") || strstr(r_wgsl_str, "r8snorm") ||
					strstr(r_wgsl_str, "r8uint") || strstr(r_wgsl_str, "r8sint") ||
					strstr(r_wgsl_str, "rg8unorm") || strstr(r_wgsl_str, "rg8snorm") ||
					strstr(r_wgsl_str, "rg8uint") || strstr(r_wgsl_str, "rg8sint"))) {
		String ws(r_wgsl_str);
		ws = ws.replace("rg8unorm", "rg32float");
		ws = ws.replace("rg8snorm", "rg32float");
		ws = ws.replace("rg8uint", "rg32uint");
		ws = ws.replace("rg8sint", "rg32sint");
		ws = ws.replace("r8unorm", "r32float");
		ws = ws.replace("r8snorm", "r32float");
		ws = ws.replace("r8uint", "r32uint");
		ws = ws.replace("r8sint", "r32sint");
		free(r_wgsl_str);
		CharString cs = ws.utf8();
		r_wgsl_str = (char *)malloc(cs.length() + 1);
		memcpy(r_wgsl_str, cs.get_data(), cs.length() + 1);
	}

	// If texture-formats-tier1 is not available, remap 16-bit SNORM/UNORM storage
	// texture format names to their float equivalents in the WGSL text.
	// Uses String::replace() (not fixed-length memcpy) so a future Tint WGSL-writer
	// spelling change can't silently corrupt output -- see Task 7.18.
	if (!has_texture_formats_tier1 &&
			(strstr(r_wgsl_str, "16snorm") || strstr(r_wgsl_str, "16unorm"))) {
		String ws(r_wgsl_str);
		ws = ws.replace("rgba16snorm", "rgba16float");
		ws = ws.replace("rgba16unorm", "rgba16float");
		ws = ws.replace("rg16snorm", "rg16float");
		ws = ws.replace("rg16unorm", "rg16float");
		ws = ws.replace("r16snorm", "r16float");
		ws = ws.replace("r16unorm", "r16float");
		free(r_wgsl_str);
		CharString cs = ws.utf8();
		r_wgsl_str = (char *)malloc(cs.length() + 1);
		memcpy(r_wgsl_str, cs.get_data(), cs.length() + 1);
	}

	// WebGPU only supports a limited set of storage texel formats (see spec §26.1.1).
	// 16-bit single/dual-channel formats (r16*, rg16*) are NOT valid for storage.
	// Remap them to 32-bit equivalents. Also handles rgba16snorm/unorm → rgba32float
	// (in case the tier1 remap above didn't already turn them into rgba16float).
	// Format names only appear in texture_storage_*<format, access> declarations in WGSL.
	// Uses String::replace() (not fixed-length memcpy) -- see Task 7.18.
	{
		String ws(r_wgsl_str);
		// RGBA16 snorm/unorm → rgba16float (rgba16float IS a valid storage format)
		ws = ws.replace("rgba16snorm", "rgba16float");
		ws = ws.replace("rgba16unorm", "rgba16float");
		// RG16 all variants → rg32 equivalents
		ws = ws.replace("rg16float", "rg32float");
		ws = ws.replace("rg16snorm", "rg32float");
		ws = ws.replace("rg16unorm", "rg32float");
		ws = ws.replace("rg16uint", "rg32uint");
		ws = ws.replace("rg16sint", "rg32sint");
		// R16 all variants → r32 equivalents
		ws = ws.replace("r16float", "r32float");
		ws = ws.replace("r16snorm", "r32float");
		ws = ws.replace("r16unorm", "r32float");
		ws = ws.replace("r16uint", "r32uint");
		ws = ws.replace("r16sint", "r32sint");
		free(r_wgsl_str);
		CharString cs = ws.utf8();
		r_wgsl_str = (char *)malloc(cs.length() + 1);
		memcpy(r_wgsl_str, cs.get_data(), cs.length() + 1);
	}

	// NOTE: vertex-stage read_write storage buffer demotion (WebGPU forbids
	// read_write/write storage bindings visible to the vertex stage; see Task
	// 9.1/9's live-repro finding about cluster_render.glsl's genuine fragment-
	// stage atomic<u32> storage buffer for why this must NOT apply to other
	// stages) used to be a text-based rewrite here. It's now applied structurally
	// at the Tint IR level instead -- see tint_ir_transforms.h's
	// DemoteVertexStageReadWriteStorage(), run inside tint_wrapper_spirv_to_wgsl()
	// before WGSL is even generated. See Task 7.18/Tier 1 #4 (webgpu_notes/TASKS.md).
}

RDD::ShaderID RenderingDeviceDriverWebGPU::shader_create_from_container(const Ref<RenderingShaderContainer> &p_shader_container, const Vector<ImmutableSampler> &p_immutable_samplers) {
	ERR_FAIL_COND_V(p_shader_container.is_null(), ShaderID());

	Ref<RenderingShaderContainerWebGPU> wg_container = p_shader_container;
	ERR_FAIL_COND_V(wg_container.is_null(), ShaderID());

	RenderingDeviceCommons::ShaderReflection shader_refl = p_shader_container->get_shader_reflection();

	WGShader *shader = new WGShader();
	shader->name = String(p_shader_container->shader_name.ptr());
	shader->push_constant_bind_group = wg_container->get_push_constant_bind_group();
	shader->push_constant_binding = wg_container->get_push_constant_binding();
	shader->push_constant_size = shader_refl.push_constant_size;

	print_verbose(vformat("WebGPU: shader_create_from_container '%s' (%d stages, push_const_size=%d)", shader->name, (int)p_shader_container->shaders.size(), (int)shader_refl.push_constant_size));

	String error_text;

	// Maps (set_index << 16 | binding) to the WGSL texture view dimension detected from Tint output.
	// Used so SAMPLER_WITH_TEXTURE and TEXTURE BGL entries get the correct viewDimension.
	HashMap<uint32_t, WGPUTextureViewDimension> wgsl_tex_dims;

	// Maps (set_index << 16 | binding) → true if the WGSL storage buffer is read-only (var<storage, read>),
	// false if read-write (var<storage, read_write>). Used to correctly set BGL buffer type for SSBOs.
	HashMap<uint32_t, bool> wgsl_ssbo_readonly;

	// Maps (set_index << 16 | binding) → storage texture access mode detected from Tint WGSL output.
	// Tint emits: texture_storage_2d<format, write/read/read_write>. Used for IMAGE BGL entries.
	HashMap<uint32_t, WGPUStorageTextureAccess> wgsl_storage_tex_access;

	// Maps (set_index << 16 | binding) → storage texture format (from WGSL scan). Used for IMAGE BGL entries.
	HashMap<uint32_t, WGPUTextureFormat> wgsl_storage_tex_format;

	// Maps (set_index << 16 | binding) → true if Tint output has var<uniform> (e.g. for texture buffers).
	HashMap<uint32_t, bool> wgsl_is_uniform;

	// Maps (set_index << 16 | binding) → true if the WGSL has texture_depth_* at this binding.
	HashMap<uint32_t, bool> wgsl_is_depth_texture;

	// Maps (set_index << 16 | binding) → true if the WGSL has sampler_comparison at this binding.
	HashMap<uint32_t, bool> wgsl_is_comparison_sampler;

	// Maps (set_index << 16 | binding) → true if the WGSL has texture_multisampled_*
	// or texture_depth_multisampled_* at this binding. Used so BGL entries set
	// texture.multisampled=true to match sampler2DMS (GLSL) bindings.
	HashMap<uint32_t, bool> wgsl_is_multisampled_texture;

	// Maps (set_index << 16 | binding) → the sampled texture's component sample
	// type, detected from Tint's texture_2d<T>/texture_3d<T>/etc. component
	// type T ("u32" → Uint, "i32" → Sint, else Float). Godot's GLSL uses
	// utexture2D/itexture2D (e.g. gi.glsl's voxel_gi_buffer) for genuinely
	// integer-sampled textures; without this, every non-depth UNIFORM_TYPE_TEXTURE
	// BGL entry unconditionally got WGPUTextureSampleType_Float regardless of
	// the real WGSL type, and Dawn rejects the mismatch at pipeline-creation
	// time ("shader's texture sample type (Uint) isn't compatible with the
	// layout's texture sample type (Float)") -- see webgpu_notes/TASKS.md Task 9.5.
	HashMap<uint32_t, WGPUTextureSampleType> wgsl_tex_sample_type;

	// Depth alias bindings: Tint splits mixed-usage depth textures into two globals
	// (one Depth at binding B, one Float alias at binding B+1). Track (set,B+1) pairs
	// so we can add extra BGL and bind group entries.
	// Maps (set_index << 16 | alias_binding) → depth_binding (the adjacent depth texture).
	HashMap<uint32_t, uint32_t> wgsl_depth_alias_bindings;

	// Maps a WGSL variable name (as Tint names it, e.g. "depth_tex" or "v_3") to its
	// (set_index << 16 | binding) key. Tint does NOT always split a depth texture that's
	// sampled both ways (textureSampleCompare *and* plain textureSample) into a separate
	// alias variable the way wgsl_depth_alias_bindings above expects -- it can just as
	// validly keep a single texture_depth_2d variable and call regular textureSample() on
	// it directly with a different, non-comparison sampler variable. This map lets the
	// call-site scan below (populating wgsl_sampler_needs_nonfiltering) resolve a texture
	// or sampler *name* seen in a textureSample(...) call back to the binding key its BGL
	// entry was built from. See webgpu_notes/TASKS.md Task 8.8.
	HashMap<String, uint32_t> wgsl_var_binding_key;

	// Maps (set_index << 16 | binding) → true if this sampler binding is statically passed
	// to a non-comparison sample call (textureSample/textureSampleLevel/textureSampleBias/
	// textureSampleGrad) together with a texture this scan has already marked Depth
	// (wgsl_is_depth_texture). WebGPU requires such a sampler's WGPUSamplerBindingType to be
	// NonFiltering, never Filtering -- GPUs commonly can't linearly filter depth formats, so
	// Dawn rejects "Texture binding ... is TextureSampleType::Depth but used statically with
	// a sampler ... that's SamplerBindingType::Filtering" at CreateRenderPipeline time. WGSL
	// itself permits calling plain textureSample() on a texture_depth_2d (it's a completely
	// valid, separate code path from textureSampleCompare() on the same variable -- e.g. a
	// depth prepass or SSAO effect reading raw depth alongside a shadow comparison
	// elsewhere in the same shader), so this can't be inferred from the texture's or
	// sampler's own declared WGSL type alone; it requires looking at the actual call sites.
	// See webgpu_notes/TASKS.md Task 8.8.
	HashMap<uint32_t, bool> wgsl_sampler_needs_nonfiltering;

	// Maps (set_index << 16 | binding) → WGPUShaderStage bitmask of stages that actually
	// declare this binding in their WGSL. Each SPIR-V stage is translated to WGSL
	// independently, so a @group/@binding only shows up in a given stage's WGSL text if
	// that resource is reachable from that stage's entry point — this gives accurate
	// per-stage BGL visibility for every uniform type (buffers, samplers, textures),
	// instead of the coarse "both Vertex and Fragment" fallback in
	// _stages_to_wgpu_visibility(). That coarse fallback overcounts fragment-only
	// samplers/textures against WebGPU's low per-stage limits (e.g. Metal enforces just
	// 8 storage buffers and 16 samplers per shader stage) and can make an otherwise-valid
	// shader's pipeline layout creation fail outright.
	HashMap<uint32_t, uint32_t> wgsl_binding_stages;

	// Read-write storage texture splits: maps (set << 16 | write_binding) → shadow_read_binding.
	// Populated when readonly-and-readwrite-storage-textures is unavailable.
	HashMap<uint32_t, uint32_t> wgsl_rw_storage_splits;

	// Read-only storage texture → sampled texture conversions.
	// Set of (set << 16 | binding) keys for read-only storage textures converted to texture_2d.
	HashSet<uint32_t> wgsl_read_storage_to_sampled;

	// --- Create one WGPUShaderModule per stage ---
	bool detected_override_declarations = false;
	Vector<RenderingShaderContainer::Shader> &stage_shaders = p_shader_container->shaders;
	for (int i = 0; i < stage_shaders.size(); i++) {
		const RenderingShaderContainer::Shader &s = stage_shaders[i];

		// The code_compressed_bytes holds raw SPIR-V (no compression — code_decompressed_size == 0).
		const PackedByteArray &spv_bytes = s.code_compressed_bytes;
		if (spv_bytes.is_empty()) {
			error_text = "WebGPU: empty SPIR-V for shader stage.";
			break;
		}
		if (spv_bytes.size() % 4 != 0) {
			error_text = "WebGPU: SPIR-V size must be a multiple of 4.";
			break;
		}

		// Store raw SPIR-V for potential re-conversion with specialization constants.
		shader->stage_spirv[(int)s.shader_stage] = spv_bytes;

		// Seed wgsl_storage_tex_format/wgsl_tex_dims with every storage image's
		// declared format and dimension straight from this stage's raw,
		// un-preprocessed SPIR-V -- before a binding that's declared but not
		// actually reachable from this stage's own entry point is stripped
		// from the WGSL by eliminate_dead_resources() below, taking with it
		// the only text the post-Tint scans further down could otherwise have
		// read this info from. Insert-if-absent: a later, post-Tint scan
		// finding real evidence for the same key always takes precedence over
		// this static declaration-only guess (e.g. a read_write split's
		// shadow companion needs the *shadow's* binding, which doesn't exist
		// yet in the original SPIR-V at all). See
		// _extract_pre_dce_storage_image_info()'s doc comment.
		for (const KeyValue<uint32_t, PreDceImageInfo> &kv : _extract_pre_dce_storage_image_info(spv_bytes.ptr(), (int)spv_bytes.size())) {
			if (kv.value.format != WGPUTextureFormat_Undefined && !wgsl_storage_tex_format.has(kv.key)) {
				wgsl_storage_tex_format[kv.key] = _promote_storage_format(kv.value.format);
			}
			if (kv.value.dim != WGPUTextureViewDimension_Undefined && !wgsl_tex_dims.has(kv.key)) {
				wgsl_tex_dims[kv.key] = kv.value.dim;
			}
			if (kv.value.sample_type != WGPUTextureSampleType_Undefined && !wgsl_tex_sample_type.has(kv.key)) {
				wgsl_tex_sample_type[kv.key] = kv.value.sample_type;
			}
		}

		// emdawnwebgpu does NOT support WGPUShaderSourceSPIRV — it's a thin wrapper
		// around the browser's WebGPU API which only accepts WGSL.
		// We convert SPIR-V → WGSL at runtime using Tint (C++, linked directly).
		// Many shader stages share SPIR-V bytes; _spv_to_wgsl_cached looks up a
		// process-lifetime cache before invoking Tint (see helper definition).
		char *wgsl_str = _spv_to_wgsl_cached(spv_bytes.ptr(), (int)spv_bytes.size());

		if (wgsl_str == nullptr) {
			error_text = vformat("WebGPU: SPIR-V→WGSL conversion failed for stage %d.", (int)s.shader_stage);
			break;
		}

		// DEPTH_ALIAS parsing removed — depth=2 images are now depth=1 in SPIR-V,
		// and a single texture_depth_2d variable handles both sampling modes.

		// Format remapping + vertex-stage read_write demotion -- see
		// _remap_unsupported_wgsl_storage_formats()'s doc comment; shared with
		// _create_module_with_spec_constants()'s own copy of this same logic.
		_remap_unsupported_wgsl_storage_formats(wgsl_str, s.shader_stage);

		// NOTE: this used to be followed by a WGSL-text-level fallback pass
		// unwrapping "binding_array<T, N>" (Chrome doesn't implement WGSL's
		// `sized_binding_array` feature Tint needs to emit that type for a GLSL
		// sampler/texture array like `sampler2DArray tex[N]`). Removed as dead
		// code (Task 9.15/Tier 1 #4 Phase 2): `spirv_preprocess::flatten_binding_arrays()`
		// (drivers/webgpu/spirv_preprocess.cpp, one of the shared SPIR-V-level
		// preprocessing passes both this function and _create_module_with_spec_constants()
		// already run via _spv_to_wgsl_cached() before Tint ever sees the bytes)
		// already eliminates every OpTypeArray/OpTypeRuntimeArray of a handle
		// type at the SPIR-V level -- structurally, with proper pointer-type
		// deduplication, not string matching -- so this WGSL-text fallback could
		// never actually fire on any real Godot shader. Confirmed empirically:
		// zero occurrences of "binding_array<" across the full ahead-of-time
		// precompiled WGSL table (wgsl_precompiled.gen.h, every declared shader
		// mode variant in the engine, 45k+ lines).

		// When readonly-and-readwrite-storage-textures is not available, split
		// read_write storage textures into separate write + read (shadow) bindings.
		// Safari rejects read_write storage texture access at BGL validation.
		// The shadow read binding uses the odd slot (B+1) which is free for IMAGE
		// types due to preprocessing's binding doubling (even=resource, odd=sampler for combined).
		if (!has_rw_storage_textures && strstr(wgsl_str, "read_write>")) {
			struct RWSplitInfo {
				uint32_t grp, bnd;
				String var_name, dim_type, fmt;
			};
			LocalVector<RWSplitInfo> rw_infos;

			// Scan for texture_storage_*<fmt,read_write> declarations.
			{
				const char *p = wgsl_str;
				while ((p = strstr(p, "@group(")) != nullptr) {
					unsigned int grp = 0, bnd = 0;
					if (!parse_group_binding(p, grp, bnd)) {
						p++;
						continue;
					}
					const char *semi = strchr(p, ';');
					if (!semi) {
						p++;
						continue;
					}
					const char *rw = strstr(p, "read_write>");
					if (!rw || rw >= semi) {
						p = semi;
						continue;
					}
					const char *ts = strstr(p, "texture_storage_");
					if (!ts || ts >= semi) {
						p = semi;
						continue;
					}
					// Variable name: "var NAME:"
					const char *vp = strstr(p, "var ");
					if (!vp || vp > ts) {
						p = semi;
						continue;
					}
					vp += 4;
					const char *colon = strchr(vp, ':');
					if (!colon || colon > ts) {
						p = semi;
						continue;
					}
					const char *ne = colon;
					while (ne > vp && (*(ne - 1) == ' ' || *(ne - 1) == '\t')) {
						ne--;
					}
					if (ne <= vp) {
						p = semi;
						continue;
					}
					// Dim type: texture_storage_Xd
					const char *lt = strchr(ts, '<');
					if (!lt || lt > rw) {
						p = semi;
						continue;
					}
					// Format: between < and , before read_write
					const char *comma = strchr(lt + 1, ',');
					if (!comma || comma >= rw) {
						p = semi;
						continue;
					}
					int fmt_len = (int)(comma - lt - 1);
					if (fmt_len <= 0 || fmt_len >= 64) {
						p = semi;
						continue;
					}

					RWSplitInfo info;
					info.grp = grp;
					info.bnd = bnd;
					{
						char buf[256];
						int len = (int)(ne - vp);
						if (len > 255) {
							len = 255;
						}
						memcpy(buf, vp, len);
						buf[len] = '\0';
						info.var_name = buf;
					}
					{
						char buf[256];
						int len = (int)(lt - ts);
						if (len > 255) {
							len = 255;
						}
						memcpy(buf, ts, len);
						buf[len] = '\0';
						info.dim_type = buf;
					}
					{
						char buf[64];
						if (fmt_len > 63) {
							fmt_len = 63;
						}
						memcpy(buf, lt + 1, fmt_len);
						buf[fmt_len] = '\0';
						info.fmt = String(buf).strip_edges();
					}
					rw_infos.push_back(info);
					p = semi;
				}
			}

			if (rw_infos.size() > 0) {
				String ws(wgsl_str);
				for (const RWSplitInfo &info : rw_infos) {
					uint32_t shadow_bnd = info.bnd + 1;
					String shadow_name = info.var_name + "_rw_in";

					// 1. Replace read_write with write in the declaration.
					ws = ws.replace(
							info.dim_type + "<" + info.fmt + ",read_write>",
							info.dim_type + "<" + info.fmt + ",write>");
					ws = ws.replace(
							info.dim_type + "<" + info.fmt + ", read_write>",
							info.dim_type + "<" + info.fmt + ", write>");

					// 2. Insert shadow read binding as a sampled texture (not storage).
					// ReadOnly storage textures require the readonly-and-readwrite-storage-textures
					// feature which Firefox lacks. A regular sampled texture works everywhere.
					String write_pat = info.dim_type + "<" + info.fmt + ",write>;";
					int64_t wt_pos = ws.find(write_pat);
					if (wt_pos == -1) {
						write_pat = info.dim_type + "<" + info.fmt + ", write>;";
						wt_pos = ws.find(write_pat);
					}
					if (wt_pos != -1) {
						int64_t insert_pos = wt_pos + write_pat.length();
						// Map texture_storage_Xd → texture_Xd and determine component type.
						String sampled_dim = info.dim_type.replace("texture_storage_", "texture_");
						String comp_type = "f32";
						if (info.fmt.find("uint") != -1) {
							comp_type = "u32";
						} else if (info.fmt.find("sint") != -1) {
							comp_type = "i32";
						}
						String shadow_decl = "\n@group(" + itos(info.grp) + ") @binding(" + itos(shadow_bnd) + ") \nvar " + shadow_name + ": " + sampled_dim + "<" + comp_type + ">;";
						ws = ws.substr(0, insert_pos) + shadow_decl + ws.substr(insert_pos);
					}

					// 3. Redirect textureLoad to the shadow (not textureStore).
					ws = ws.replace("textureLoad(" + info.var_name + ",", "textureLoad(" + shadow_name + ",");
					ws = ws.replace("textureLoad(" + info.var_name + ")", "textureLoad(" + shadow_name + ")");

					// 4. Sampled textures need a mip level parameter in textureLoad.
					// textureLoad(shadow, coords) → textureLoad(shadow, coords, 0)
					{
						String search = "textureLoad(" + shadow_name;
						int64_t pos = 0;
						while ((pos = ws.find(search, pos)) != -1) {
							int64_t paren_start = pos + String("textureLoad").length();
							int depth = 0;
							int64_t insert_mip = -1;
							for (int64_t k = paren_start; k < ws.length(); k++) {
								if (ws[k] == '(') {
									depth++;
								} else if (ws[k] == ')') {
									depth--;
									if (depth == 0) {
										insert_mip = k;
										break;
									}
								}
							}
							if (insert_mip != -1) {
								ws = ws.substr(0, insert_mip) + ", 0" + ws.substr(insert_mip);
								pos = insert_mip + 3;
							} else {
								pos += search.length();
							}
						}
					}

					// Record the split for BGL/bind-group creation. The shadow's
					// format must be recorded too (keyed by its OWN binding, not
					// the write-side binding) -- the BGL-construction code below
					// looks it up via wgsl_storage_tex_format[shadow_key] to pick
					// the shadow's sampleType, but nothing else ever wrote to that
					// map for the shadow's key: its own WGSL declaration is a
					// synthetic `texture_2d<T>` with no format literal at all (see
					// shadow_decl above), so the general format-scanning pass has
					// nothing to find here either. Left unrecorded, the lookup fell
					// through to the RGBA8Unorm/UnfilterableFloat default,
					// mismatching the shadow's real component type (e.g. Sint for
					// an rgba16sint source, as in sdfgi_integrate.glsl's
					// lightprobe_history_texture) against what Tint actually
					// declared it as. See webgpu_notes/TASKS.md Task 9.5 Round 7.
					uint32_t key = ((uint32_t)info.grp << 16) | info.bnd;
					wgsl_rw_storage_splits[key] = shadow_bnd;
					uint32_t shadow_key = ((uint32_t)info.grp << 16) | shadow_bnd;
					wgsl_storage_tex_format[shadow_key] = _wgsl_storage_format_string_to_wgpu(info.fmt);
				}
				// Log the transformed WGSL for debugging.
				WEBGPU_DIAG({ console.log('[RW-SPLIT] Split ' + $0 + ' read_write storage texture(s)'); }, (int)rw_infos.size());
				print_verbose("WebGPU rw_storage split WGSL:\n" + ws);

				free(wgsl_str);
				CharString cs = ws.utf8();
				wgsl_str = (char *)malloc(cs.length() + 1);
				memcpy(wgsl_str, cs.get_data(), cs.length() + 1);
			}
		}

		// When readonly-and-readwrite-storage-textures is not available, convert
		// read-only storage textures to sampled textures. Firefox/wgpu rejects
		// texture_storage_*<fmt, read> without the feature.
		if (!has_rw_storage_textures && (strstr(wgsl_str, ", read>") || strstr(wgsl_str, ",read>"))) {
			struct ReadOnlyInfo {
				uint32_t grp, bnd;
				String var_name, dim_type, fmt;
			};
			LocalVector<ReadOnlyInfo> ro_infos;

			// Scan for texture_storage_*<fmt, read> declarations.
			{
				const char *p = wgsl_str;
				while ((p = strstr(p, "@group(")) != nullptr) {
					unsigned int grp = 0, bnd = 0;
					if (!parse_group_binding(p, grp, bnd)) {
						p++;
						continue;
					}
					const char *semi = strchr(p, ';');
					if (!semi) {
						p++;
						continue;
					}
					// Look for ", read>" or ",read>" but NOT "read_write>"
					const char *rd = strstr(p, ",read>");
					if (!rd || rd >= semi) {
						rd = strstr(p, ", read>");
					}
					if (!rd || rd >= semi) {
						p = semi;
						continue;
					}
					// Make sure it's not read_write (already handled above).
					if (rd > wgsl_str && *(rd - 1) == '_') {
						p = semi;
						continue;
					} // part of "read_write"
					// Check for "read_write>" pattern — skip if found.
					const char *rw_check = strstr(p, "read_write>");
					if (rw_check && rw_check < semi && rw_check < rd) {
						p = semi;
						continue;
					}

					const char *ts = strstr(p, "texture_storage_");
					if (!ts || ts >= semi) {
						p = semi;
						continue;
					}
					// Variable name: "var NAME:"
					const char *vp = strstr(p, "var ");
					if (!vp || vp > ts) {
						p = semi;
						continue;
					}
					vp += 4;
					const char *colon = strchr(vp, ':');
					if (!colon || colon > ts) {
						p = semi;
						continue;
					}
					const char *ne = colon;
					while (ne > vp && (*(ne - 1) == ' ' || *(ne - 1) == '\t')) {
						ne--;
					}
					if (ne <= vp) {
						p = semi;
						continue;
					}
					// Dim type: texture_storage_Xd
					const char *lt = strchr(ts, '<');
					if (!lt || lt > rd) {
						p = semi;
						continue;
					}
					// Format: between < and , before read
					// Note: rd points to ",read>" (includes comma) so comma == rd is expected.
					// Use > not >= so the format-access separator comma is correctly accepted.
					const char *comma = strchr(lt + 1, ',');
					if (!comma || comma > rd) {
						p = semi;
						continue;
					}
					int fmt_len = (int)(comma - lt - 1);
					if (fmt_len <= 0 || fmt_len >= 64) {
						p = semi;
						continue;
					}

					ReadOnlyInfo info;
					info.grp = grp;
					info.bnd = bnd;
					{
						char buf[256];
						int len = (int)(ne - vp);
						if (len > 255) {
							len = 255;
						}
						memcpy(buf, vp, len);
						buf[len] = '\0';
						info.var_name = buf;
					}
					{
						char buf[256];
						int len = (int)(lt - ts);
						if (len > 255) {
							len = 255;
						}
						memcpy(buf, ts, len);
						buf[len] = '\0';
						info.dim_type = buf;
					}
					{
						char buf[64];
						if (fmt_len > 63) {
							fmt_len = 63;
						}
						memcpy(buf, lt + 1, fmt_len);
						buf[fmt_len] = '\0';
						info.fmt = String(buf).strip_edges();
					}
					ro_infos.push_back(info);
					p = semi;
				}
			}

			if (ro_infos.size() > 0) {
				String ws(wgsl_str);
				for (const ReadOnlyInfo &info : ro_infos) {
					// Map texture_storage_Xd → texture_Xd and determine component type.
					String sampled_dim = info.dim_type.replace("texture_storage_", "texture_");
					String comp_type = "f32";
					if (info.fmt.find("uint") != -1) {
						comp_type = "u32";
					} else if (info.fmt.find("sint") != -1) {
						comp_type = "i32";
					}

					// Replace the full declaration:
					//   texture_storage_Xd<fmt, read> → texture_Xd<comp_type>
					// Try both spacing variants.
					String old_decl = info.dim_type + "<" + info.fmt + ", read>";
					String new_decl = sampled_dim + "<" + comp_type + ">";
					ws = ws.replace(old_decl, new_decl);
					old_decl = info.dim_type + "<" + info.fmt + ",read>";
					ws = ws.replace(old_decl, new_decl);

					// Add mip level parameter to textureLoad calls for this variable.
					// textureLoad(var, coords) → textureLoad(var, coords, 0)
					{
						String search = "textureLoad(" + info.var_name;
						int64_t pos = 0;
						while ((pos = ws.find(search, pos)) != -1) {
							// Guard against one converted variable's name being a
							// literal prefix of another's (e.g. sdfgi's "src_light"
							// vs. "src_light_aniso") -- without this, searching for
							// "textureLoad(src_light" also matches inside
							// "textureLoad(src_light_aniso, ...)"'s own call,
							// appending a second, spurious mip-level argument to it
							// (produces an invalid 4-argument textureLoad on a
							// texture_3d<T>, which only takes 3). See
							// webgpu_notes/TASKS.md Task 9.5.
							int64_t after_name = pos + search.length();
							char32_t next_char = after_name < ws.length() ? ws[after_name] : 0;
							bool is_ident_continuation = next_char == '_' ||
									(next_char >= 'a' && next_char <= 'z') ||
									(next_char >= 'A' && next_char <= 'Z') ||
									(next_char >= '0' && next_char <= '9');
							if (is_ident_continuation) {
								pos += search.length();
								continue;
							}
							int64_t paren_start = pos + String("textureLoad").length();
							int depth = 0;
							int64_t insert_mip = -1;
							for (int64_t k = paren_start; k < ws.length(); k++) {
								if (ws[k] == '(') {
									depth++;
								} else if (ws[k] == ')') {
									depth--;
									if (depth == 0) {
										insert_mip = k;
										break;
									}
								}
							}
							if (insert_mip != -1) {
								ws = ws.substr(0, insert_mip) + ", 0" + ws.substr(insert_mip);
								pos = insert_mip + 3;
							} else {
								pos += search.length();
							}
						}
					}

					// Record the conversion for BGL creation and populate the
					// format map so the BGL entry gets the correct sample type.
					// The WGSL declaration is about to be removed, so the later
					// format-scanning pass won't find it — we must record it now.
					uint32_t key = ((uint32_t)info.grp << 16) | info.bnd;
					wgsl_read_storage_to_sampled.insert(key);
					wgsl_storage_tex_format[key] = _wgsl_storage_format_string_to_wgpu(info.fmt);
				}
				WEBGPU_DIAG({ console.log('[READ-CONVERT] Converted ' + $0 + ' read-only storage texture(s) to sampled'); }, (int)ro_infos.size());
				print_verbose("WebGPU read_storage→sampled WGSL:\n" + ws);

				free(wgsl_str);
				CharString cs = ws.utf8();
				wgsl_str = (char *)malloc(cs.length() + 1);
				memcpy(wgsl_str, cs.get_data(), cs.length() + 1);
			}
		}

		// See _reclassify_single_component_depth_textures()'s doc comment
		// (Task 7.13): rewrites texture_2d<f32> bindings that are only ever
		// read as a single scalar component to texture_depth_2d, so a real
		// depth-format texture bound there (e.g. Bokeh DOF's source_depth)
		// gets used directly instead of the Depth/Float mismatch fallback
		// silently substituting a meaningless 4x4 dummy texture.
		// Also exclude the "_rw_in" shadow-read companion textures the
		// read_write-storage-texture split (just above) synthesizes for the
		// read side of a split binding (e.g. voxel_gi.glsl's "depth" storage
		// image becomes "depth"+"depth_rw_in") -- these are exactly as
		// vulnerable to the depth-naming false positive as the read-storage-
		// to-sampled conversion's outputs are, and for the identical reason:
		// a synthetic sampled-texture binding whose name happens to contain
		// "depth" but which was never a real depth-format GLSL binding. See
		// webgpu_notes/TASKS.md Task 9.5 Round 6.
		HashSet<uint32_t> wgsl_reclassify_exclude;
		for (uint32_t k : wgsl_read_storage_to_sampled) {
			wgsl_reclassify_exclude.insert(k);
		}
		for (const KeyValue<uint32_t, uint32_t> &kv : wgsl_rw_storage_splits) {
			uint32_t shadow_key = (kv.key & 0xFFFF0000u) | kv.value;
			wgsl_reclassify_exclude.insert(shadow_key);
		}
		wgsl_str = _reclassify_single_component_depth_textures(wgsl_str, wgsl_reclassify_exclude);

		WGPUShaderSourceWGSL wgsl_source = {};
		wgsl_source.chain.sType = WGPUSType_ShaderSourceWGSL;
		wgsl_source.code = WGPUStringView{ wgsl_str, WGPU_STRLEN };

		WGPUShaderModuleDescriptor mod_desc = {};
		mod_desc.nextInChain = (WGPUChainedStruct *)&wgsl_source;

		// Label the module so Dawn error messages (and JS-side patch logs) identify it.
		String _mod_label = "mod:" + shader->name + ":stg" + itos((int)s.shader_stage);
		CharString _mod_label_cs = _mod_label.utf8();
		mod_desc.label = { _mod_label_cs.get_data(), WGPU_STRLEN };

		WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &mod_desc);

		// Scan WGSL for texture dimension declarations so the BGL uses the right viewDimension.
		// Tint format: "@group(G) @binding(B) var NAME: texture_TYPE<...>;"
		// Also detects sampler / sampler_comparison types.
		{
			const char *p = wgsl_str;
			while ((p = strstr(p, "@group(")) != nullptr) {
				unsigned int grp = 0, bnd = 0;
				if (parse_group_binding(p, grp, bnd)) {
					// Find the ':' that separates the variable name from the type.
					// This avoids matching "sampler" in variable names like "shadow_sampler".
					const char *colon = strchr(p, ':');
					const char *semi = strchr(p, ';');
					if (!semi) {
						p++;
						continue;
					}
					const char *type_start = colon && colon < semi ? colon + 1 : p;
					const char *limit = semi; // scan up to the semicolon

					const char *fwd = type_start;
					const char *tp = nullptr;
					const char *sp = nullptr; // sampler type position
					while (fwd < limit && *fwd) {
						if (!tp && strncmp(fwd, "texture_", 8) == 0) {
							tp = fwd;
							break;
						}
						if (!sp && strncmp(fwd, "sampler_comparison", 18) == 0) {
							sp = fwd;
							break;
						}
						if (!sp && strncmp(fwd, "sampler", 7) == 0 && fwd[7] != '_') {
							sp = fwd;
							break;
						}
						fwd++;
					}
					// Check for comparison sampler (sampler_comparison type).
					if (sp && strncmp(sp, "sampler_comparison", 18) == 0) {
						uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
						wgsl_is_comparison_sampler[key] = true;
					}
					if (tp) {
						WGPUTextureViewDimension dim = WGPUTextureViewDimension_Undefined;
						// Multisampled variants first (must precede plain texture_2d / texture_depth_2d).
						if (strncmp(tp, "texture_depth_multisampled_2d", 29) == 0) {
							dim = WGPUTextureViewDimension_2D;
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_is_multisampled_texture[key] = true;
						} else if (strncmp(tp, "texture_multisampled_2d", 23) == 0) {
							dim = WGPUTextureViewDimension_2D;
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_is_multisampled_texture[key] = true;
						} else if (strncmp(tp, "texture_depth_2d_array", 22) == 0) {
							dim = WGPUTextureViewDimension_2DArray;
						} else if (strncmp(tp, "texture_depth_cube_array", 24) == 0) {
							dim = WGPUTextureViewDimension_CubeArray;
						} else if (strncmp(tp, "texture_depth_cube", 18) == 0) {
							dim = WGPUTextureViewDimension_Cube;
						} else if (strncmp(tp, "texture_depth_2d", 16) == 0) {
							dim = WGPUTextureViewDimension_2D;
						} else if (strncmp(tp, "texture_2d_array", 16) == 0) {
							dim = WGPUTextureViewDimension_2DArray;
						} else if (strncmp(tp, "texture_cube_array", 18) == 0) {
							dim = WGPUTextureViewDimension_CubeArray;
						} else if (strncmp(tp, "texture_cube", 12) == 0) {
							dim = WGPUTextureViewDimension_Cube;
						} else if (strncmp(tp, "texture_3d", 10) == 0) {
							dim = WGPUTextureViewDimension_3D;
						} else if (strncmp(tp, "texture_2d", 10) == 0) {
							dim = WGPUTextureViewDimension_2D;
						} else if (strncmp(tp, "texture_1d", 10) == 0) {
							dim = WGPUTextureViewDimension_1D;
						} else if (strncmp(tp, "texture_storage_2d_array", 24) == 0) {
							dim = WGPUTextureViewDimension_2DArray;
						} else if (strncmp(tp, "texture_storage_cube_array", 26) == 0) {
							dim = WGPUTextureViewDimension_CubeArray;
						} else if (strncmp(tp, "texture_storage_2d", 18) == 0) {
							dim = WGPUTextureViewDimension_2D;
						} else if (strncmp(tp, "texture_storage_3d", 18) == 0) {
							dim = WGPUTextureViewDimension_3D;
						}
						if (dim != WGPUTextureViewDimension_Undefined) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							if (!wgsl_tex_dims.has(key)) {
								wgsl_tex_dims[key] = dim;
							} else if (wgsl_tex_dims[key] != dim) {
								// Different stages have different types for the same binding — use the later stage value.
								wgsl_tex_dims[key] = dim;
							}
						}
						// Check if this is a depth texture (texture_depth_*).
						bool is_depth_tp = tp && strncmp(tp, "texture_depth_", 14) == 0;
						if (is_depth_tp) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_is_depth_texture[key] = true;
						}
						// Detect the sampled component type from the texture_*<T> generic
						// parameter (depth and storage textures have no such component-type
						// parameter, so skip them -- depth is always Depth sampleType, and
						// storage textures use a completely different BGL field).
						if (tp && !is_depth_tp && strncmp(tp, "texture_storage_", 16) != 0) {
							const char *angle = (const char *)memchr(tp, '<', (size_t)(limit - tp));
							if (angle) {
								WGPUTextureSampleType stype = WGPUTextureSampleType_Float;
								if (strncmp(angle + 1, "u32", 3) == 0) {
									stype = WGPUTextureSampleType_Uint;
								} else if (strncmp(angle + 1, "i32", 3) == 0) {
									stype = WGPUTextureSampleType_Sint;
								}
								uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
								wgsl_tex_sample_type[key] = stype;
							}
						}
					}
					// Check for depth alias variable: Tint names it "*_depth_alias".
					// The variable name is between "var " and ":".
					{
						const char *var_kw = strstr(p, "var ");
						if (var_kw && var_kw < semi) {
							const char *name_start = var_kw + 4;
							// Skip any <...> (e.g., var<uniform>)
							if (*name_start == '<') {
								const char *gt = strchr(name_start, '>');
								if (gt && gt < semi) {
									name_start = gt + 1;
									while (name_start < semi && *name_start == ' ') {
										name_start++;
									}
								}
							}
							int name_len = 0;
							const char *nc = name_start;
							while (nc < semi && *nc != ':' && *nc != ' ') {
								nc++;
								name_len++;
							}
							if (name_len > 0) {
								uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
								wgsl_var_binding_key[String::utf8(name_start, name_len)] = key;
							}
							if (name_len > 12) { // "_depth_alias" is 12 chars
								const char *suffix = name_start + name_len - 12;
								if (strncmp(suffix, "_depth_alias", 12) == 0) {
									uint32_t alias_key = ((uint32_t)grp << 16) | (uint32_t)bnd;
									uint32_t depth_bnd = bnd > 0 ? bnd - 1 : 0;
									wgsl_depth_alias_bindings[alias_key] = depth_bnd;
								}
							}
						}
					}
				}
				p++;
			}
		}

		// Find samplers statically passed to a non-comparison sample call together
		// with a texture already marked Depth above (wgsl_is_depth_texture) -- these
		// need WGPUSamplerBindingType_NonFiltering, not the default Filtering. See
		// wgsl_sampler_needs_nonfiltering's declaration for why this can't be inferred
		// from either variable's own declared WGSL type. textureSampleCompare /
		// textureSampleCompareLevel are deliberately excluded: those pair a texture
		// with a sampler_comparison, already handled by wgsl_is_comparison_sampler.
		// textureGather( is included -- its texture_depth_2d overload (Task 7.22)
		// is (tex, sampler, coords), same shape as the others, and was missing here
		// entirely: found via a real Chrome run against ss_effects_downsample.glsl,
		// whose source_depth is read via a mix of texelFetch (no sampler reference
		// in WGSL at all, so nothing to detect) and textureGather (which does
		// reference the sampler, but wasn't scanned) -- the paired sampler kept its
		// default Filtering type, which WebGPU rejects for any static use with a
		// Depth-sampleType texture binding. See webgpu_notes/TASKS.md Task 9.5 Round 10.
		{
			static const char *const non_compare_fns[] = {
				"textureSample(", "textureSampleLevel(", "textureSampleBias(", "textureSampleGrad(", "textureGather("
			};
			for (const char *fn : non_compare_fns) {
				const size_t fn_len = strlen(fn);
				const char *p = wgsl_str;
				while ((p = strstr(p, fn)) != nullptr) {
					const char *args = p + fn_len;
					p = args;
					// Handle-typed call arguments in Tint's WGSL output are always plain
					// identifiers (variable/parameter references) -- WGSL doesn't allow
					// computed expressions for texture/sampler handles -- so a simple
					// identifier scan is sufficient here.
					auto read_ident = [&args]() -> String {
						while (*args == ' ' || *args == '\t' || *args == '\n') {
							args++;
						}
						const char *start = args;
						while ((*args >= 'a' && *args <= 'z') || (*args >= 'A' && *args <= 'Z') || (*args >= '0' && *args <= '9') || *args == '_') {
							args++;
						}
						return String::utf8(start, (int)(args - start));
					};
					String tex_name = read_ident();
					while (*args == ' ') {
						args++;
					}
					if (*args != ',' || tex_name.is_empty()) {
						continue;
					}
					args++;
					String samp_name = read_ident();
					if (samp_name.is_empty() || !wgsl_var_binding_key.has(tex_name) || !wgsl_var_binding_key.has(samp_name)) {
						continue;
					}
					uint32_t tex_key = wgsl_var_binding_key[tex_name];
					if (wgsl_is_depth_texture.has(tex_key) && wgsl_is_depth_texture[tex_key]) {
						wgsl_sampler_needs_nonfiltering[wgsl_var_binding_key[samp_name]] = true;
					}
				}
			}
		}

		// Which WGPUShaderStage this stage's separately-translated WGSL module
		// corresponds to. Used below (see wgsl_binding_stages) to record, per binding,
		// which stages actually declare it.
		WGPUShaderStage current_wgpu_stage = WGPUShaderStage_None;
		if (s.shader_stage == RDD::SHADER_STAGE_VERTEX) {
			current_wgpu_stage = WGPUShaderStage_Vertex;
		} else if (s.shader_stage == RDD::SHADER_STAGE_FRAGMENT) {
			current_wgpu_stage = WGPUShaderStage_Fragment;
		} else if (s.shader_stage == RDD::SHADER_STAGE_COMPUTE) {
			current_wgpu_stage = WGPUShaderStage_Compute;
		}

		// Scan WGSL for storage buffer access modes and uniform bindings.
		// Tint actual output formats:
		//   - Read-write: "var<storage, read_write>"  (space after comma)
		//   - Read-only:  "var<storage>"              (NO access modifier — Tint omits "read" for LOAD-only)
		{
			const char *p = wgsl_str;
			while ((p = strstr(p, "@group(")) != nullptr) {
				unsigned int grp = 0, bnd = 0;
				if (parse_group_binding(p, grp, bnd)) {
					uint32_t stage_key = ((uint32_t)grp << 16) | (uint32_t)bnd;
					if (wgsl_binding_stages.has(stage_key)) {
						wgsl_binding_stages[stage_key] |= (uint32_t)current_wgpu_stage;
					} else {
						wgsl_binding_stages[stage_key] = (uint32_t)current_wgpu_stage;
					}

					const char *fwd = p;
					const char *limit = fwd + 256;
					while (fwd < limit && *fwd && *fwd != ';') {
						if (strncmp(fwd, "var<storage, read_write>", 24) == 0 ||
								strncmp(fwd, "var<storage,read_write>", 23) == 0) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_ssbo_readonly[key] = false;
							break;
						} else if (strncmp(fwd, "var<storage, read>", 18) == 0 ||
								strncmp(fwd, "var<storage,read>", 17) == 0) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_ssbo_readonly[key] = true;
							break;
						} else if (strncmp(fwd, "var<storage>", 12) == 0) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_ssbo_readonly[key] = true;
							break;
						} else if (strncmp(fwd, "var<uniform", 11) == 0) {
							uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
							wgsl_is_uniform[key] = true;
							break;
						}
						fwd++;
					}
				}
				p++;
			}
		}

		// Scan WGSL for storage texture access mode and format.
		// Tint format: "@group(G) @binding(B) var name: texture_storage_*<format, access>;"
		{
			const char *p = wgsl_str;
			while ((p = strstr(p, "@group(")) != nullptr) {
				unsigned int grp = 0, bnd = 0;
				if (parse_group_binding(p, grp, bnd)) {
					const char *fwd = p;
					const char *limit = fwd + 300;
					while (fwd < limit && *fwd && *fwd != ';') {
						if (strncmp(fwd, "texture_storage_", 16) == 0) {
							const char *lt = strchr(fwd, '<');
							if (lt && lt < limit) {
								// Extract format (between < and ,)
								const char *comma = strchr(lt + 1, ',');
								if (comma && comma < limit) {
									uint32_t key = ((uint32_t)grp << 16) | (uint32_t)bnd;
									// Parse format name
									const char *fmt = lt + 1;
									WGPUTextureFormat tf = WGPUTextureFormat_RGBA8Unorm; // fallback
									if (strncmp(fmt, "rgba8unorm,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA8Unorm;
									} else if (strncmp(fmt, "rgba8snorm,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA8Snorm;
									} else if (strncmp(fmt, "rgba8uint,", 10) == 0) {
										tf = WGPUTextureFormat_RGBA8Uint;
									} else if (strncmp(fmt, "rgba8sint,", 10) == 0) {
										tf = WGPUTextureFormat_RGBA8Sint;
									} else if (strncmp(fmt, "rgba16float,", 12) == 0) {
										tf = WGPUTextureFormat_RGBA16Float;
									} else if (strncmp(fmt, "rgba16uint,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA16Uint;
									} else if (strncmp(fmt, "rgba16sint,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA16Sint;
									} else if (strncmp(fmt, "rgba32float,", 12) == 0) {
										tf = WGPUTextureFormat_RGBA32Float;
									} else if (strncmp(fmt, "rgba32uint,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA32Uint;
									} else if (strncmp(fmt, "rgba32sint,", 11) == 0) {
										tf = WGPUTextureFormat_RGBA32Sint;
									} else if (strncmp(fmt, "rg32float,", 10) == 0) {
										tf = WGPUTextureFormat_RG32Float;
									} else if (strncmp(fmt, "rg32uint,", 9) == 0) {
										tf = WGPUTextureFormat_RG32Uint;
									} else if (strncmp(fmt, "rg32sint,", 9) == 0) {
										tf = WGPUTextureFormat_RG32Sint;
									} else if (strncmp(fmt, "r32float,", 9) == 0) {
										tf = WGPUTextureFormat_R32Float;
									} else if (strncmp(fmt, "r32uint,", 8) == 0) {
										tf = WGPUTextureFormat_R32Uint;
									} else if (strncmp(fmt, "r32sint,", 8) == 0) {
										tf = WGPUTextureFormat_R32Sint;
									} else if (strncmp(fmt, "r16float,", 9) == 0) {
										tf = WGPUTextureFormat_R16Float;
									} else if (strncmp(fmt, "r16uint,", 8) == 0) {
										tf = WGPUTextureFormat_R16Uint;
									} else if (strncmp(fmt, "r16sint,", 8) == 0) {
										tf = WGPUTextureFormat_R16Sint;
									} else if (strncmp(fmt, "r16snorm,", 9) == 0) {
										tf = WGPUTextureFormat_R16Float; // Fallback
									} else if (strncmp(fmt, "r16unorm,", 9) == 0) {
										tf = WGPUTextureFormat_R16Float; // Fallback
									} else if (strncmp(fmt, "r8unorm,", 8) == 0) {
										tf = WGPUTextureFormat_R8Unorm;
									} else if (strncmp(fmt, "r8snorm,", 8) == 0) {
										tf = WGPUTextureFormat_R8Snorm;
									} else if (strncmp(fmt, "r8uint,", 7) == 0) {
										tf = WGPUTextureFormat_R8Uint;
									} else if (strncmp(fmt, "r8sint,", 7) == 0) {
										tf = WGPUTextureFormat_R8Sint;
									} else if (strncmp(fmt, "rg8unorm,", 9) == 0) {
										tf = WGPUTextureFormat_RG8Unorm;
									} else if (strncmp(fmt, "rg8snorm,", 9) == 0) {
										tf = WGPUTextureFormat_RG8Snorm;
									} else if (strncmp(fmt, "rg8uint,", 8) == 0) {
										tf = WGPUTextureFormat_RG8Uint;
									} else if (strncmp(fmt, "rg8sint,", 8) == 0) {
										tf = WGPUTextureFormat_RG8Sint;
									} else if (strncmp(fmt, "rg16float,", 10) == 0) {
										tf = WGPUTextureFormat_RG16Float;
									} else if (strncmp(fmt, "rg16uint,", 9) == 0) {
										tf = WGPUTextureFormat_RG16Uint;
									} else if (strncmp(fmt, "rg16sint,", 9) == 0) {
										tf = WGPUTextureFormat_RG16Sint;
									} else if (strncmp(fmt, "rg16snorm,", 10) == 0) {
										tf = WGPUTextureFormat_RG16Float; // Fallback
									} else if (strncmp(fmt, "rg16unorm,", 10) == 0) {
										tf = WGPUTextureFormat_RG16Float; // Fallback
									} else if (strncmp(fmt, "rgba16snorm,", 12) == 0) {
										tf = WGPUTextureFormat_RGBA16Float; // Fallback
									} else if (strncmp(fmt, "rgba16unorm,", 12) == 0) {
										tf = WGPUTextureFormat_RGBA16Float; // Fallback
									} else if (strncmp(fmt, "bgra8unorm,", 11) == 0) {
										tf = WGPUTextureFormat_BGRA8Unorm;
									}
									// rgb10a2unorm/rgb10a2uint/rg11b10ufloat were missing entirely,
									// silently falling through to the RGBA8Unorm default above --
									// found via a real Chrome run where octmap_downsampler.glsl /
									// octmap_filter.glsl's OCTMAP_FORMAT=rgb10_a2 variant (ShaderRD
									// compiles both the rgb10_a2 and rgba16f variants eagerly, same
									// "dead but still compiled" pattern as Task 9.5's FSR1 fix) hit
									// "layout's binding format RGBA8Unorm doesn't match shader's
									// binding format RGB10A2Unorm". See webgpu_notes/TASKS.md Task
									// 9.5 Round 9.
									else if (strncmp(fmt, "rgb10a2unorm,", 13) == 0) {
										tf = WGPUTextureFormat_RGB10A2Unorm;
									} else if (strncmp(fmt, "rgb10a2uint,", 12) == 0) {
										tf = WGPUTextureFormat_RGB10A2Uint;
									} else if (strncmp(fmt, "rg11b10ufloat,", 14) == 0) {
										tf = WGPUTextureFormat_RG11B10Ufloat;
									}
									wgsl_storage_tex_format[key] = tf;
									// Parse access mode (after comma, skip space)
									const char *acc = comma + 1;
									while (*acc == ' ') {
										acc++;
									}
									WGPUStorageTextureAccess access = WGPUStorageTextureAccess_Undefined;
									if (strncmp(acc, "read_write>", 11) == 0) {
										access = WGPUStorageTextureAccess_ReadWrite;
									} else if (strncmp(acc, "write>", 6) == 0) {
										access = WGPUStorageTextureAccess_WriteOnly;
									} else if (strncmp(acc, "read>", 5) == 0) {
										access = WGPUStorageTextureAccess_ReadOnly;
									}
									if (access != WGPUStorageTextureAccess_Undefined) {
										wgsl_storage_tex_access[key] = access;
									}
								}
							}
							break;
						}
						fwd++;
					}
				}
				p++;
			}
		}

		// Extract per-stage override constant IDs from WGSL (@id(N) override ...).
		// These are present when Tint preserved specialization constants as overrides.
		// Only include overrides whose variable is actually referenced in the shader
		// body (beyond the declaration line). Safari's WebGPU WGSL→MSL compiler
		// crashes when pipeline constants are passed for unreferenced overrides.
		{
			const char *scan = wgsl_str;
			while ((scan = strstr(scan, "@id(")) != nullptr) {
				scan += 4; // skip "@id("
				uint32_t oid = 0;
				bool has_digits = false;
				while (*scan >= '0' && *scan <= '9') {
					oid = oid * 10 + (*scan - '0');
					scan++;
					has_digits = true;
				}
				if (has_digits && *scan == ')') {
					detected_override_declarations = true;
					// Extract variable name: skip ") override " then read name up to ":".
					const char *p = scan + 1; // skip ')'
					while (*p == ' ' || *p == '\t' || *p == '\n') {
						p++;
					}
					if (strncmp(p, "override", 8) == 0) {
						p += 8;
						while (*p == ' ' || *p == '\t') {
							p++;
						}
						const char *name_start = p;
						while (*p != ':' && *p != '\0' && *p != ' ' && *p != '\n') {
							p++;
						}
						int name_len = (int)(p - name_start);
						if (name_len > 0 && name_len < 256) {
							// Copy the variable name to a null-terminated buffer for strstr.
							char name_buf[256];
							memcpy(name_buf, name_start, name_len);
							name_buf[name_len] = '\0';
							// Check if this variable name is referenced beyond its declaration.
							// Search from after the declaration line (past the ';').
							const char *decl_end = p;
							while (*decl_end && *decl_end != '\n') {
								decl_end++;
							}
							bool found_usage = false;
							const char *search = decl_end;
							while ((search = strstr(search, name_buf)) != nullptr) {
								// Verify it's a whole-word match (not a substring of another identifier).
								if (search + name_len <= wgsl_str + strlen(wgsl_str)) {
									char before = (search > wgsl_str) ? *(search - 1) : ' ';
									char after = *(search + name_len);
									bool boundary_before = !(before >= 'a' && before <= 'z') && !(before >= 'A' && before <= 'Z') && !(before >= '0' && before <= '9') && before != '_';
									bool boundary_after = !(after >= 'a' && after <= 'z') && !(after >= 'A' && after <= 'Z') && !(after >= '0' && after <= '9') && after != '_';
									if (boundary_before && boundary_after) {
										found_usage = true;
										break;
									}
								}
								search++;
							}
							if (found_usage) {
								shader->stage_override_ids[s.shader_stage].insert(oid);
							}
						} else {
							// Fallback: couldn't parse name, include the override to be safe.
							shader->stage_override_ids[s.shader_stage].insert(oid);
						}
					} else {
						// Not followed by "override" keyword — include to be safe.
						shader->stage_override_ids[s.shader_stage].insert(oid);
					}
				}
			}
		}

		free(wgsl_str); // Free the EM_ASM-allocated string.
		if (mod == nullptr) {
			error_text = vformat("WebGPU: wgpuDeviceCreateShaderModule failed for stage %d.", (int)s.shader_stage);
			break;
		}

		if (s.shader_stage < 6) {
			shader->stage_modules[s.shader_stage] = mod;
		}
		// Set the legacy module alias to the first created module.
		if (!shader->module) {
			shader->module = mod;
		}
	}

	shader->has_override_declarations = detected_override_declarations;
	if (detected_override_declarations) {
		print_verbose(vformat("WebGPU: shader '%s' has override declarations — will use pipeline constants for specialization.", shader->name));
	}

	if (!error_text.is_empty()) {
		goto cleanup;
	}

	{ // Block scope: variables here (LocalVector, String) have non-trivial
	  // destructors and must not be jumped over by goto.

		// --- Build WGPUBindGroupLayout for each descriptor set ---
		const uint32_t set_count = (uint32_t)shader_refl.uniform_sets.size();
		shader->bind_group_infos.resize(set_count);
		shader->bind_group_layouts.resize(set_count);

		// Resolves per-stage BGL visibility for a given (set<<16|binding) key from the
		// actual per-stage WGSL scan (wgsl_binding_stages), falling back to the coarse
		// per-uniform-type visibility (p_fallback, from Godot's SPIR-V reflection) only
		// when no stage's translated WGSL was scanned at all. See wgsl_binding_stages
		// declaration above for why this matters more than it might look.
		// Function-scoped (not loop-local) so the merged push-constant-group rebuild
		// below (the `for (uint32_t i = 0; i < total_groups; i++)` loop) can reuse it too.
		auto resolve_stage_visibility = [&](uint32_t p_key, WGPUShaderStage p_fallback) -> WGPUShaderStage {
			if (wgsl_binding_stages.has(p_key)) {
				return (WGPUShaderStage)wgsl_binding_stages[p_key];
			} else if (!wgsl_binding_stages.is_empty()) {
				// Declared in SPIR-V reflection but not found in any translated stage's
				// WGSL — not actually reachable from any entry point. Visibility None
				// doesn't count against any stage's resource limit.
				return (WGPUShaderStage)0;
			}
			return p_fallback;
		};
		// Resolves a sampler binding's WGPUSamplerBindingType: Comparison if
		// wgsl_is_comparison_sampler detected it, NonFiltering if the call-site scan
		// found it statically paired with a Depth texture (wgsl_sampler_needs_nonfiltering
		// — see that map's declaration for why WebGPU requires this), otherwise the
		// ordinary Filtering default. Function-scoped for the same reason as above.
		auto resolve_sampler_type = [&](uint32_t p_key) -> WGPUSamplerBindingType {
			if (wgsl_is_comparison_sampler.has(p_key) && wgsl_is_comparison_sampler[p_key]) {
				return WGPUSamplerBindingType_Comparison;
			}
			if (wgsl_sampler_needs_nonfiltering.has(p_key) && wgsl_sampler_needs_nonfiltering[p_key]) {
				return WGPUSamplerBindingType_NonFiltering;
			}
			return WGPUSamplerBindingType_Filtering;
		};

		for (uint32_t set = 0; set < set_count; set++) {
			const Vector<RenderingDeviceCommons::ShaderUniform> &set_uniforms = shader_refl.uniform_sets[set];

			// Count entries — combined sampler+texture expands to 2 entries each,
			// EXCEPT a multisampled combined sampler (GLSL sampler2DMS), which has
			// no paired sampler variable at all (see the UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
			// case below) and expands to just 1.
			uint32_t entry_count = 0;
			for (int i = 0; i < set_uniforms.size(); i++) {
				if (set_uniforms[i].type == RDD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE) {
					uint32_t k0 = ((uint32_t)set << 16) | (set_uniforms[i].binding * 2 + 0);
					bool ms_no_sampler = wgsl_is_multisampled_texture.has(k0) && wgsl_is_multisampled_texture[k0];
					entry_count += ms_no_sampler ? 1 : 2;
				} else {
					entry_count += 1;
				}
			}

			LocalVector<WGPUBindGroupLayoutEntry> entries;
			entries.resize(entry_count);

			WGShader::BindGroupInfo &bgi = shader->bind_group_infos[set];
			bgi.entries.resize(set_uniforms.size());

			uint32_t e_idx = 0;
			for (int u_idx = 0; u_idx < set_uniforms.size(); u_idx++) {
				const RenderingDeviceCommons::ShaderUniform &u = set_uniforms[u_idx];
				WGShader::BindGroupEntry &bge = bgi.entries[u_idx];
				WGPUShaderStage vis = _stages_to_wgpu_visibility((uint32_t)u.stages);

				bge.godot_type = u.type;

				switch (u.type) {
					case RDD::UNIFORM_TYPE_SAMPLER: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						// FLATTEN-BA pass removes all binding_array<T,N> from WGSL,
						// so layout entries are always non-array (no bindingArraySize).
						entry.sampler.type = resolve_sampler_type(k);
						bge.layout_entry = entry;
						bge.array_length = 1;
					} break;

					case RDD::UNIFORM_TYPE_TEXTURE:
					case RDD::UNIFORM_TYPE_INPUT_ATTACHMENT: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						// FLATTEN-BA pass removes all binding_array<T,N> from WGSL,
						// so layout entries are always non-array (no bindingArraySize).
						bool is_ms = wgsl_is_multisampled_texture.has(k) && wgsl_is_multisampled_texture[k];
						bool is_depth = wgsl_is_depth_texture.has(k) && wgsl_is_depth_texture[k];
						// Real sample type from the WGSL texture_2d<T>/etc. component type
						// (Uint/Sint for GLSL utexture*/itexture*, e.g. gi.glsl's
						// voxel_gi_buffer -- Float otherwise). Multisampled *float*
						// textures must use UnfilterableFloat, not Float (filtering is
						// illegal for MSAA textures in WebGPU); Uint/Sint are already
						// inherently non-filterable, so MSAA doesn't change them further.
						WGPUTextureSampleType base_sample_type = wgsl_tex_sample_type.has(k) ? wgsl_tex_sample_type[k] : WGPUTextureSampleType_Float;
						entry.texture.sampleType = is_depth
								? WGPUTextureSampleType_Depth
								: (is_ms && base_sample_type == WGPUTextureSampleType_Float) ? WGPUTextureSampleType_UnfilterableFloat
																							 : base_sample_type;
						entry.texture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
						entry.texture.multisampled = is_ms;
						bge.layout_entry = entry;
						bge.array_length = 1;
					} break;

					case RDD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
						// Combined sampler+texture split by our SPIR-V preprocessor:
						// Sampler at binding*2+0, texture at binding*2+1 in the modified SPIR-V → matches Tint WGSL output.
						//
						// EXCEPT for multisampled combined samplers (GLSL sampler2DMS):
						// texelFetch-only multisampled access needs no sampler object at
						// all (WGSL's texture_multisampled_2d has no sampling functions,
						// only textureLoad), so split_combined_samplers()
						// (spirv_preprocess.cpp) emits no sampler variable for these and
						// places the split-out texture at binding*2 instead of *2+1. This
						// code used to unconditionally assume sampler-at-*2/texture-at-
						// *2+1 regardless, producing a BGL that expected a sampler at
						// binding*2 when Tint's WGSL actually declared a
						// texture_multisampled_2d there -- "shader (texture) doesn't
						// match layout (sampler)". Found via a real Chrome run against
						// resolve.glsl's MODE_RESOLVE_GI source_normal_roughness. See
						// webgpu_notes/TASKS.md Task 9.5 Round 9.
						uint32_t k0 = ((uint32_t)set << 16) | (u.binding * 2 + 0);
						bool ms_no_sampler = wgsl_is_multisampled_texture.has(k0) && wgsl_is_multisampled_texture[k0];

						WGPUBindGroupLayoutEntry samp_entry = {};
						if (!ms_no_sampler) {
							samp_entry.binding = u.binding * 2 + 0;
							samp_entry.visibility = resolve_stage_visibility(k0, vis);
							samp_entry.sampler.type = resolve_sampler_type(k0);
							entries[e_idx++] = samp_entry;
						}

						WGPUBindGroupLayoutEntry &tex_entry = entries[e_idx++];
						tex_entry = {};
						uint32_t tex_binding = ms_no_sampler ? (u.binding * 2 + 0) : (u.binding * 2 + 1);
						tex_entry.binding = tex_binding;
						uint32_t k1 = ((uint32_t)set << 16) | tex_binding;
						tex_entry.visibility = resolve_stage_visibility(k1, vis);
						{
							bool is_ms = ms_no_sampler || (wgsl_is_multisampled_texture.has(k1) && wgsl_is_multisampled_texture[k1]);
							bool is_depth = wgsl_is_depth_texture.has(k1) && wgsl_is_depth_texture[k1];
							// See the UNIFORM_TYPE_TEXTURE case above for why this isn't an
							// unconditional Float/UnfilterableFloat choice.
							WGPUTextureSampleType base_sample_type = wgsl_tex_sample_type.has(k1) ? wgsl_tex_sample_type[k1] : WGPUTextureSampleType_Float;
							tex_entry.texture.sampleType = is_depth
									? WGPUTextureSampleType_Depth
									: (is_ms && base_sample_type == WGPUTextureSampleType_Float) ? WGPUTextureSampleType_UnfilterableFloat
																								 : base_sample_type;
							tex_entry.texture.viewDimension = wgsl_tex_dims.has(k1) ? wgsl_tex_dims[k1] : WGPUTextureViewDimension_2D;
							tex_entry.texture.multisampled = is_ms;
							// MSAA texture bindings with UnfilterableFloat require a NonFiltering sampler —
							// override the sampler for this combined binding (irrelevant when
							// ms_no_sampler, since no sampler entry exists at all then).
							if (is_ms && !is_depth && !ms_no_sampler) {
								samp_entry.sampler.type = WGPUSamplerBindingType_NonFiltering;
							}
						}

						bge.layout_entry = tex_entry; // Store texture entry as the primary.
						bge.paired_sampler_entry = samp_entry;
						bge.has_paired_sampler_entry = !ms_no_sampler;
					} break;

					case RDD::UNIFORM_TYPE_IMAGE: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						if (wgsl_read_storage_to_sampled.has(k)) {
							// Read-only storage texture was converted to sampled texture_2d.
							// Create a sampled texture BGL entry instead of storage.
							WGPUTextureFormat fmt = wgsl_storage_tex_format.has(k) ? wgsl_storage_tex_format[k] : WGPUTextureFormat_RGBA8Unorm;
							entry.texture.sampleType = _texture_sample_type_for_format(fmt);
							entry.texture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
							entry.texture.multisampled = false;
						} else {
							WGPUTextureFormat fmt = wgsl_storage_tex_format.has(k) ? wgsl_storage_tex_format[k] : WGPUTextureFormat_RGBA8Unorm;
							WGPUStorageTextureAccess access = wgsl_storage_tex_access.has(k)
									? wgsl_storage_tex_access[k]
									: (u.writable ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadOnly);
							entry.storageTexture.access = access;
							entry.storageTexture.format = fmt;
							entry.storageTexture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
						}
						bge.layout_entry = entry;
					} break;

					case RDD::UNIFORM_TYPE_UNIFORM_BUFFER: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						entry.visibility = resolve_stage_visibility(((uint32_t)set << 16) | (u.binding * 2), vis);
						entry.buffer.type = WGPUBufferBindingType_Uniform;
						entry.buffer.hasDynamicOffset = false;
						entry.buffer.minBindingSize = 0;
						bge.layout_entry = entry;
					} break;

					case RDD::UNIFORM_TYPE_STORAGE_BUFFER: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						// Check if WGSL scan shows this is actually a storage texture.
						if (wgsl_storage_tex_format.has(k)) {
							WGPUTextureFormat fmt = wgsl_storage_tex_format[k];
							WGPUStorageTextureAccess access = wgsl_storage_tex_access.has(k)
									? wgsl_storage_tex_access[k]
									: (u.writable ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadOnly);
							entry.storageTexture.access = access;
							entry.storageTexture.format = fmt;
							entry.storageTexture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;

						} else if (wgsl_is_uniform.has(k) && wgsl_is_uniform[k]) {
							// Tint emitted var<uniform> for this binding.
							entry.buffer.type = WGPUBufferBindingType_Uniform;
						} else {
							bool is_readonly = wgsl_ssbo_readonly.has(k) ? wgsl_ssbo_readonly[k] : !u.writable;
							entry.buffer.type = is_readonly ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
						}
						bge.layout_entry = entry;
					} break;

					// Task 7.5: Dynamic variants — hasDynamicOffset=true allows the
					// bind group to be set with per-frame offsets via wgpuRenderPassEncoderSetBindGroup.
					case RDD::UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						entry.visibility = resolve_stage_visibility(((uint32_t)set << 16) | (u.binding * 2), vis);
						entry.buffer.type = WGPUBufferBindingType_Uniform;
						entry.buffer.hasDynamicOffset = true;
						entry.buffer.minBindingSize = 0;
						bge.layout_entry = entry;
					} break;

					case RDD::UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						bool is_storage_tex = false;
						{
							if (wgsl_storage_tex_format.has(k)) {
								is_storage_tex = true;
								WGPUTextureFormat fmt = wgsl_storage_tex_format[k];
								WGPUStorageTextureAccess access = wgsl_storage_tex_access.has(k)
										? wgsl_storage_tex_access[k]
										: (u.writable ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadOnly);
								entry.storageTexture.access = access;
								entry.storageTexture.format = fmt;
								entry.storageTexture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
							} else if (wgsl_is_uniform.has(k) && wgsl_is_uniform[k]) {
								entry.buffer.type = WGPUBufferBindingType_Uniform;
							} else {
								bool is_readonly = wgsl_ssbo_readonly.has(k) ? wgsl_ssbo_readonly[k] : !u.writable;
								entry.buffer.type = is_readonly ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
							}
						}
						// Only buffer bindings support dynamic offsets; storage textures must not set it.
						entry.buffer.hasDynamicOffset = !is_storage_tex;
						entry.buffer.minBindingSize = 0;
						bge.layout_entry = entry;
					} break;

					// WebGPU has no texel buffers (TBOs); emulate as uniform or storage buffers based on WGSL.
					case RDD::UNIFORM_TYPE_TEXTURE_BUFFER:
					case RDD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE_BUFFER: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						// Tint may convert TBOs to var<uniform> or var<storage,read> depending on usage.
						{
							if (wgsl_is_uniform.has(k) && wgsl_is_uniform[k]) {
								entry.buffer.type = WGPUBufferBindingType_Uniform;
							} else if (wgsl_ssbo_readonly.has(k)) {
								entry.buffer.type = wgsl_ssbo_readonly[k] ? WGPUBufferBindingType_ReadOnlyStorage : WGPUBufferBindingType_Storage;
							} else {
								entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage; // default fallback
							}
						}
						entry.buffer.hasDynamicOffset = false;
						entry.buffer.minBindingSize = 0;
						bge.layout_entry = entry;
					} break;

					case RDD::UNIFORM_TYPE_IMAGE_BUFFER: {
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						uint32_t k = ((uint32_t)set << 16) | (u.binding * 2);
						entry.visibility = resolve_stage_visibility(k, vis);
						// Tint may convert image buffers to texture_storage_*.
						if (wgsl_storage_tex_format.has(k)) {
							WGPUTextureFormat fmt = wgsl_storage_tex_format[k];
							WGPUStorageTextureAccess access = wgsl_storage_tex_access.has(k)
									? wgsl_storage_tex_access[k]
									: (u.writable ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadOnly);
							entry.storageTexture.access = access;
							entry.storageTexture.format = fmt;
							entry.storageTexture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
						} else {
							entry.buffer.type = WGPUBufferBindingType_Storage;
							entry.buffer.hasDynamicOffset = false;
							entry.buffer.minBindingSize = 0;
						}
						bge.layout_entry = entry;
					} break;

					default:
						WARN_PRINT_ONCE(vformat("WebGPU: unhandled uniform type %d in bind group layout.", (int)u.type));
						WGPUBindGroupLayoutEntry &entry = entries[e_idx++];
						entry = {};
						entry.binding = u.binding * 2; // Preprocessing doubles all non-combined bindings.
						entry.visibility = vis;
						entry.buffer.type = WGPUBufferBindingType_Uniform;
						bge.layout_entry = entry;
						break;
				}
			}

			// Add BGL entries for depth alias variables.
			// Tint splits mixed-usage depth textures: original→Depth at binding B,
			// clone→Float at binding B+1 (named "*_depth_alias").
			for (const KeyValue<uint32_t, uint32_t> &kv : wgsl_depth_alias_bindings) {
				uint32_t alias_key = kv.key;
				uint32_t alias_grp = alias_key >> 16;
				uint32_t alias_bnd = alias_key & 0xFFFF;
				if (alias_grp != set) {
					continue;
				}
				WGPUBindGroupLayoutEntry alias_entry = {};
				alias_entry.binding = alias_bnd;
				alias_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
				alias_entry.texture.sampleType = WGPUTextureSampleType_Float;
				alias_entry.texture.viewDimension = wgsl_tex_dims.has(alias_key) ? wgsl_tex_dims[alias_key] : WGPUTextureViewDimension_2D;
				alias_entry.texture.multisampled = false;
				entries.push_back(alias_entry);
			}

			// Add BGL entries for read_write storage texture shadow read bindings.
			// Each shadow is a sampled texture at (write_binding + 1), NOT a ReadOnly
			// storage texture, because ReadOnly access requires the
			// readonly-and-readwrite-storage-textures feature which Firefox lacks.
			for (const KeyValue<uint32_t, uint32_t> &kv : wgsl_rw_storage_splits) {
				uint32_t write_key = kv.key;
				uint32_t shadow_bnd = kv.value;
				uint32_t split_grp = write_key >> 16;
				if (split_grp != set) {
					continue;
				}
				uint32_t shadow_key = ((uint32_t)split_grp << 16) | shadow_bnd;
				WGPUTextureFormat shadow_fmt = wgsl_storage_tex_format.has(shadow_key) ? wgsl_storage_tex_format[shadow_key] : WGPUTextureFormat_RGBA8Unorm;
				WGPUBindGroupLayoutEntry shadow_entry = {};
				shadow_entry.binding = shadow_bnd;
				// The shadow is read from exactly the same stage(s) as the original
				// read_write binding it shadows -- not always Compute alone (e.g.
				// scene_forward_clustered_inc.glsl's geom_facing_grid is read_write
				// from the Fragment stage during SDFGI/VoxelGI voxelization). Reuse
				// the write-side binding's own resolved visibility rather than
				// hardcoding Compute, which only happened to be correct for every
				// read_write-split binding seen before this one. See Task 9.5 Round 23.
				shadow_entry.visibility = resolve_stage_visibility(write_key, WGPUShaderStage_Compute);
				shadow_entry.texture.sampleType = _texture_sample_type_for_format(shadow_fmt);
				shadow_entry.texture.viewDimension = wgsl_tex_dims.has(shadow_key) ? wgsl_tex_dims[shadow_key] : WGPUTextureViewDimension_2D;
				shadow_entry.texture.multisampled = false;
				entries.push_back(shadow_entry);
			}

			// Log any duplicate binding indices for debugging.
			for (uint32_t a = 0; a < entries.size(); a++) {
				for (uint32_t b = a + 1; b < entries.size(); b++) {
					if (entries[a].binding == entries[b].binding) {
						WEBGPU_DIAG({ console.error('[BGL-DUP] set=' + $0 + ' binding=' + $1 + ' idx_a=' + $2 + ' idx_b=' + $3); }, (int)set, (int)entries[a].binding, (int)a, (int)b);
					}
				}
			}

			WGPUBindGroupLayoutDescriptor layout_desc = {};
			layout_desc.entryCount = entries.size();
			layout_desc.entries = entries.size() > 0 ? entries.ptr() : nullptr;

			// Label the BGL so JS-side patch logs identify it.
			String _bgl_label = "bgl:" + shader->name + ":set" + itos((int)set);
			CharString _bgl_label_cs = _bgl_label.utf8();
			layout_desc.label = { _bgl_label_cs.get_data(), WGPU_STRLEN };

			shader->bind_group_layouts[set] = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);
			if (shader->bind_group_layouts[set] == nullptr) {
				error_text = "WebGPU: wgpuDeviceCreateBindGroupLayout failed.";
				goto cleanup;
			}
		}

		// Store depth alias bindings on the shader for use during uniform_set_create.
		shader->depth_alias_bindings = wgsl_depth_alias_bindings;

		// Store read_write storage texture splits for bind group creation.
		shader->rw_storage_splits = wgsl_rw_storage_splits;
		shader->read_storage_to_sampled = wgsl_read_storage_to_sampled;

		// --- Build WGPUPipelineLayout ---
		// The number of bind groups in the pipeline layout must cover:
		//   - All descriptor sets (0 .. set_count - 1)
		//   - The push constant bind group slot (if any)
		const bool has_pc = wg_container->has_push_constants();
		const uint32_t pc_group = has_pc ? wg_container->get_push_constant_bind_group() : UINT32_MAX;
		const uint32_t total_groups = has_pc ? MAX(set_count, pc_group + 1) : set_count;

		LocalVector<WGPUBindGroupLayout> all_layouts;
		all_layouts.resize(total_groups);

		// Use the persistent empty layout for gap slots between sets and push constant slot.

		for (uint32_t i = 0; i < total_groups; i++) {
			if (has_pc && i == pc_group) {
				// Build a merged layout: the shader's own group entries (material uniforms,
				// textures, etc.) PLUS the PC ring-buffer entry at binding 0 with hasDynamicOffset.
				// This is needed because calling setBindGroup() twice for the same group index
				// overrides the first binding — we must combine both into one layout/bind group.
				bool has_existing = (i < set_count && i < (uint32_t)shader->bind_group_infos.size() &&
						!shader->bind_group_infos[i].entries.is_empty());
				if (has_existing) {
					// Rebuild all layout entries for this group (both sampler AND texture for
					// combined types — bge.layout_entry only stores the texture entry).
					LocalVector<WGPUBindGroupLayoutEntry> merged_entries;
					const Vector<RenderingDeviceCommons::ShaderUniform> &pc_uniforms = shader_refl.uniform_sets[i];
					for (int u_idx2 = 0; u_idx2 < pc_uniforms.size(); u_idx2++) {
						const RenderingDeviceCommons::ShaderUniform &pu = pc_uniforms[u_idx2];
						WGPUShaderStage pvis_fallback = _stages_to_wgpu_visibility((uint32_t)pu.stages);
						if (pu.type == RDD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE) {
							WGPUBindGroupLayoutEntry se = {}, te = {};
							se.binding = pu.binding * 2 + 0;
							se.visibility = resolve_stage_visibility(((uint32_t)i << 16) | (pu.binding * 2 + 0), pvis_fallback);
							{
								uint32_t k = ((uint32_t)i << 16) | (pu.binding * 2 + 0);
								se.sampler.type = resolve_sampler_type(k);
							}
							te.binding = pu.binding * 2 + 1;
							te.visibility = resolve_stage_visibility(((uint32_t)i << 16) | (pu.binding * 2 + 1), pvis_fallback);
							{
								uint32_t k = ((uint32_t)i << 16) | (pu.binding * 2 + 1);
								bool is_ms = wgsl_is_multisampled_texture.has(k) && wgsl_is_multisampled_texture[k];
								bool is_depth = wgsl_is_depth_texture.has(k) && wgsl_is_depth_texture[k];
								WGPUTextureSampleType base_sample_type = wgsl_tex_sample_type.has(k) ? wgsl_tex_sample_type[k] : WGPUTextureSampleType_Float;
								te.texture.sampleType = is_depth
										? WGPUTextureSampleType_Depth
										: (is_ms && base_sample_type == WGPUTextureSampleType_Float) ? WGPUTextureSampleType_UnfilterableFloat
																									 : base_sample_type;
								te.texture.viewDimension = wgsl_tex_dims.has(k) ? wgsl_tex_dims[k] : WGPUTextureViewDimension_2D;
								te.texture.multisampled = is_ms;
								if (is_ms && !is_depth) {
									se.sampler.type = WGPUSamplerBindingType_NonFiltering;
								}
							}
							merged_entries.push_back(se);
							merged_entries.push_back(te);
						} else {
							// For all other types, just copy the existing bge.layout_entry.
							merged_entries.push_back(shader->bind_group_infos[i].entries[u_idx2].layout_entry);
						}
					}
					// Add the PC ring-buffer entry: PUSH_CONSTANT_RING_BINDING, uniform, hasDynamicOffset=true.
					// Safety: skip if any existing entry is already at PUSH_CONSTANT_RING_BINDING.
					bool has_pc_binding = false;
					for (const auto &me : merged_entries) {
						if (me.binding == PUSH_CONSTANT_RING_BINDING) {
							has_pc_binding = true;
							break;
						}
					}
					WGPUBindGroupLayoutEntry pc_entry = {};
					pc_entry.binding = PUSH_CONSTANT_RING_BINDING;
					pc_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
					pc_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
					pc_entry.buffer.hasDynamicOffset = true;
					pc_entry.buffer.minBindingSize = 0;
					if (!has_pc_binding) {
						merged_entries.push_back(pc_entry);
					}

					// Add depth alias entries to the merged layout.
					for (const KeyValue<uint32_t, uint32_t> &kv : wgsl_depth_alias_bindings) {
						uint32_t alias_grp = kv.key >> 16;
						uint32_t alias_bnd = kv.key & 0xFFFF;
						if (alias_grp != i) {
							continue;
						}
						WGPUBindGroupLayoutEntry ae = {};
						ae.binding = alias_bnd;
						ae.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
						ae.texture.sampleType = WGPUTextureSampleType_Float;
						ae.texture.viewDimension = wgsl_tex_dims.has(kv.key) ? wgsl_tex_dims[kv.key] : WGPUTextureViewDimension_2D;
						ae.texture.multisampled = false;
						merged_entries.push_back(ae);
					}
					// Add rw_storage split entries to the merged layout (sampled texture, not ReadOnly storage).
					for (const KeyValue<uint32_t, uint32_t> &kv : wgsl_rw_storage_splits) {
						uint32_t split_grp = kv.key >> 16;
						uint32_t shadow_bnd = kv.value;
						if (split_grp != i) {
							continue;
						}
						uint32_t shadow_key = ((uint32_t)split_grp << 16) | shadow_bnd;
						WGPUTextureFormat shadow_fmt = wgsl_storage_tex_format.has(shadow_key) ? wgsl_storage_tex_format[shadow_key] : WGPUTextureFormat_RGBA8Unorm;
						WGPUBindGroupLayoutEntry se = {};
						se.binding = shadow_bnd;
						// See the matching fix (and its full explanation) on the
						// primary shadow_entry construction above -- same reasoning
						// applies to this merged-layout copy. Task 9.5 Round 23.
						se.visibility = resolve_stage_visibility(kv.key, WGPUShaderStage_Compute);
						se.texture.sampleType = _texture_sample_type_for_format(shadow_fmt);
						se.texture.viewDimension = wgsl_tex_dims.has(shadow_key) ? wgsl_tex_dims[shadow_key] : WGPUTextureViewDimension_2D;
						se.texture.multisampled = false;
						merged_entries.push_back(se);
					}

					WGPUBindGroupLayoutDescriptor merged_desc = {};
					merged_desc.entryCount = merged_entries.size();
					merged_desc.entries = merged_entries.ptr();
					shader->merged_pc_group_layout = wgpuDeviceCreateBindGroupLayout(device, &merged_desc);
					if (!shader->merged_pc_group_layout) {
						error_text = "WebGPU: failed to create merged PC+material bind group layout.";
						goto cleanup;
					}
					all_layouts[i] = shader->merged_pc_group_layout;
				} else {
					// No material uniforms at this group — use the universal PC-only layout.
					all_layouts[i] = push_constant_bind_group_layout;
				}
			} else if (i < set_count) {
				// Check if this set is empty (no uniforms defined in GLSL).
				// E.g., CanvasShaderRD has sets 0, 2, 3 but no set 1.
				bool is_empty_set = (i >= (uint32_t)shader->bind_group_infos.size() ||
						shader->bind_group_infos[i].entries.is_empty());
				if (is_empty_set) {
					all_layouts[i] = empty_bind_group_layout;
					shader->gap_bind_group_indices.push_back(i);
				} else {
					all_layouts[i] = shader->bind_group_layouts[i];
				}
			} else {
				// Gap slot — use empty layout and record for pre-binding at draw time.
				all_layouts[i] = empty_bind_group_layout;
				shader->gap_bind_group_indices.push_back(i);
			}
		}

		WGPUPipelineLayoutDescriptor pl_desc = {};
		pl_desc.bindGroupLayoutCount = total_groups;
		pl_desc.bindGroupLayouts = all_layouts.size() > 0 ? all_layouts.ptr() : nullptr;

		// Label the pipeline layout for JS-side patch logs.
		String _pl_label = "plyt:" + shader->name;
		CharString _pl_label_cs = _pl_label.utf8();
		pl_desc.label = { _pl_label_cs.get_data(), WGPU_STRLEN };

		shader->pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pl_desc);
		if (shader->pipeline_layout == nullptr) {
			error_text = "WebGPU: wgpuDeviceCreatePipelineLayout failed.";
			goto cleanup;
		}

		return ShaderID(shader);

	} // End block scope.

cleanup:
	// Clean up partially-constructed shader (mirrors shader_free).
	for (int i = 0; i < 6; i++) {
		if (shader->stage_modules[i]) {
			wgpuShaderModuleRelease(shader->stage_modules[i]);
		}
	}
	if (shader->pipeline_layout) {
		wgpuPipelineLayoutRelease(shader->pipeline_layout);
	}
	for (WGPUBindGroupLayout &layout : shader->bind_group_layouts) {
		if (layout) {
			wgpuBindGroupLayoutRelease(layout);
		}
	}
	if (shader->merged_pc_group_layout) {
		wgpuBindGroupLayoutRelease(shader->merged_pc_group_layout);
	}
	delete shader;
	ERR_FAIL_V_MSG(ShaderID(), error_text);
}

uint32_t RenderingDeviceDriverWebGPU::shader_get_layout_hash(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	if (!shader) {
		return 0;
	}
	// Use the pipeline layout pointer as a cheap hash identifier.
	return (uint32_t)(uint64_t)(void *)(shader->pipeline_layout);
}

void RenderingDeviceDriverWebGPU::shader_free(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_NULL(shader);
	for (int i = 0; i < 6; i++) {
		if (shader->stage_modules[i]) {
			wgpuShaderModuleRelease(shader->stage_modules[i]);
			shader->stage_modules[i] = nullptr;
		}
	}
	if (shader->pipeline_layout) {
		wgpuPipelineLayoutRelease(shader->pipeline_layout);
	}
	for (WGPUBindGroupLayout &layout : shader->bind_group_layouts) {
		if (layout) {
			wgpuBindGroupLayoutRelease(layout);
		}
	}
	if (shader->merged_pc_group_layout) {
		wgpuBindGroupLayoutRelease(shader->merged_pc_group_layout);
	}
	delete shader;
}

void RenderingDeviceDriverWebGPU::shader_destroy_modules(ShaderID p_shader) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_NULL(shader);
	for (int i = 0; i < 6; i++) {
		if (shader->stage_modules[i]) {
			wgpuShaderModuleRelease(shader->stage_modules[i]);
			shader->stage_modules[i] = nullptr;
		}
	}
	shader->module = nullptr;
}

// =============================================================================
// UNIFORM SET
// =============================================================================

// Creates the shadow-copy texture/view for one read_write-storage-split
// binding and fills r_entry with the resulting WGPUBindGroupEntry (bound at
// the split's shadow binding number). Shared by uniform_set_create() (the
// normal case: building a brand-new bind group) and _get_compatible_bind_group()
// (adapting a cached bind group to a *different*, layout-compatible shader
// variant that needs a shadow entry the original variant never created --
// see that function's own call site for why this case exists at all: two
// ShaderRD mode variants sharing the same uniform interface can disagree on
// which bindings are actually read_write in their own DCE'd WGSL, since
// read_write-ness is a per-specialization property of which code paths a
// given #ifdef mode actually exercises, not a fixed property of the binding
// number. Task 9.5 Round 24.
bool RenderingDeviceDriverWebGPU::_create_rw_shadow_bind_entry(WGUniformSet *p_us, WGTexture *p_orig_tex, uint32_t p_shadow_bnd, WGPUBindGroupEntry &r_entry) {
	// depthOrArrayLayers must come from p_orig_tex->layers for a 2D(-array)
	// texture -- p_orig_tex->depth is only meaningful for TEXTURE_TYPE_3D
	// (mirrors the same dimension-aware selection texture_create() uses when
	// first building this same texture's own WGPUTextureDescriptor). Task 9.5
	// Round 22.
	WGPUTextureDescriptor shadow_desc = {};
	shadow_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
	shadow_desc.dimension = p_orig_tex->dimension;
	shadow_desc.size = { p_orig_tex->width, p_orig_tex->height, (p_orig_tex->dimension == WGPUTextureDimension_3D) ? p_orig_tex->depth : p_orig_tex->layers };
	shadow_desc.format = p_orig_tex->format;
	shadow_desc.mipLevelCount = p_orig_tex->mipmaps;
	shadow_desc.sampleCount = p_orig_tex->sample_count;
	WGPUTexture shadow_tex = wgpuDeviceCreateTexture(device, &shadow_desc);
	if (!shadow_tex) {
		WARN_PRINT("WebGPU: failed to create shadow read texture for rw_storage split.");
		return false;
	}
	// NOTE: We intentionally do NOT copy original→shadow here. The shadow
	// will be populated by command_copy_buffer_to_texture (called from
	// texture_update) before any compute dispatch reads it. An eager copy
	// here races with later wgpuQueueWriteTexture on Firefox/wgpu,
	// overwriting shadow data with zeros.
	WGPUTextureViewDescriptor vd = {};
	vd.format = p_orig_tex->format;
	vd.dimension = p_orig_tex->view_dimension;
	vd.baseMipLevel = 0;
	vd.mipLevelCount = p_orig_tex->mipmaps;
	vd.baseArrayLayer = 0;
	vd.arrayLayerCount = p_orig_tex->layers;
	vd.aspect = WGPUTextureAspect_All;
	WGPUTextureView shadow_view = wgpuTextureCreateView(shadow_tex, &vd);
	if (!shadow_view) {
		wgpuTextureRelease(shadow_tex);
		return false;
	}
	r_entry = {};
	r_entry.binding = p_shadow_bnd;
	r_entry.textureView = shadow_view;
	p_us->rw_shadow_textures.push_back(shadow_tex);
	p_us->rw_shadow_views.push_back(shadow_view);
	// Register in the driver-level map so future texture_update calls on the
	// source also copy to this shadow.
	rw_shadow_copy_map[p_orig_tex->gpu_handle()].push_back(shadow_tex);
	// Same dimension-aware depthOrArrayLayers selection as shadow_desc.size
	// above -- p_orig_tex->depth is always 1 for a 2D(-array) texture, so a
	// multi-layer source (e.g. sdfgi_integrate.glsl's 30-layer
	// lightprobe_history_texture) would otherwise only ever have layer 0
	// refreshed by command_bind_compute_uniform_sets()'s per-bind copy below,
	// leaving every other layer's shadow permanently stale. Task 9.5 Round 46.
	uint32_t shadow_copy_depth = (p_orig_tex->dimension == WGPUTextureDimension_3D) ? p_orig_tex->depth : p_orig_tex->layers;
	p_us->rw_shadow_registrations.push_back({ p_orig_tex->gpu_handle(), shadow_tex, p_orig_tex->width, p_orig_tex->height, shadow_copy_depth });
	return true;
}

RDD::UniformSetID RenderingDeviceDriverWebGPU::uniform_set_create(VectorView<BoundUniform> p_uniforms, ShaderID p_shader, uint32_t p_set_index, int p_linear_pool_index) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_NULL_V(shader, UniformSetID());
	ERR_FAIL_COND_V(p_set_index >= (uint32_t)shader->bind_group_layouts.size(), UniformSetID());

	WGPUBindGroupLayout layout = shader->bind_group_layouts[p_set_index];
	ERR_FAIL_NULL_V(layout, UniformSetID());

	// If this set is also the push constant group AND the shader has a merged layout
	// (PC ring buffer + material uniforms combined), switch to the merged layout.
	// We will also inject a PC ring buffer entry into the entries array below.
	const bool is_pc_group = (shader->push_constant_size > 0 &&
			p_set_index == shader->push_constant_bind_group &&
			shader->merged_pc_group_layout != nullptr);
	if (is_pc_group) {
		layout = shader->merged_pc_group_layout;
	}

	// Each BoundUniform may expand to one or two WGPUBindGroupEntry items.
	LocalVector<WGPUBindGroupEntry> entries;
	entries.reserve(p_uniforms.size() * 2);

	// Allocate the uniform set early so texture handlers can store temp views.
	WGUniformSet *us = new WGUniformSet();
	us->set_index = p_set_index;

	// Track WGPUBuffer handles that have already been bound as STORAGE_BUFFER in this set.
	// WebGPU forbids aliased writable storage buffer bindings in a single dispatch
	// (Vulkan allows this via barriers). When the same buffer appears a second time,
	// redirect it to aliasing_stub_buffer so the bind group passes validation.
	HashMap<WGPUBuffer, uint32_t> dup_storage_seen; // buffer handle → first uniform index

	for (uint32_t i = 0; i < p_uniforms.size(); i++) {
		const BoundUniform &uniform = p_uniforms[i];
		// WebGPU has no immutable sampler concept — always provide the sampler
		// in the bind group (unlike Vulkan where it's baked into the pipeline layout).

		switch (uniform.type) {
			case UNIFORM_TYPE_SAMPLER: {
				// Tint flattens binding arrays to single resources, so only provide
				// the first sampler. Multiple IDs at the same binding means a
				// binding array which is reduced to 1 element.
				if (uniform.ids.size() > 0) {
					WGPUBindGroupEntry entry = {};
					entry.binding = uniform.binding * 2;
					entry.sampler = (WGPUSampler)(uniform.ids[0].id);

					// If this shader's own BGL determined the binding is used statically
					// with a depth texture via non-comparison sampling, it requires
					// WGPUSamplerBindingType_NonFiltering (see Task 8.8/8.9). Godot has no
					// NonFiltering concept at the material level and just passes whatever
					// (usually linear-filtering) sampler RID the uniform set was given for
					// this slot -- substitute the dedicated nearest dummy sampler instead,
					// unconditionally, since any filtering sampler here fails validation.
					if (p_set_index < (uint32_t)shader->bind_group_infos.size() && dummy_nonfiltering_sampler) {
						for (const auto &bge : shader->bind_group_infos[p_set_index].entries) {
							if (bge.layout_entry.binding == entry.binding &&
									bge.layout_entry.sampler.type == WGPUSamplerBindingType_NonFiltering) {
								entry.sampler = dummy_nonfiltering_sampler;
								break;
							}
						}
					}

					entries.push_back(entry);
				}
			} break;

			case UNIFORM_TYPE_TEXTURE:
			case UNIFORM_TYPE_INPUT_ATTACHMENT: {
				// Tint flattens binding arrays to single resources.
				// Only provide the first texture for array bindings.
				WGPUTextureViewDimension expected_dim = WGPUTextureViewDimension_Undefined;
				WGPUTextureSampleType expected_sample = WGPUTextureSampleType_Undefined;
				if (p_set_index < (uint32_t)shader->bind_group_infos.size()) {
					for (const auto &bge : shader->bind_group_infos[p_set_index].entries) {
						if (bge.layout_entry.binding == uniform.binding * 2) {
							expected_dim = bge.layout_entry.texture.viewDimension;
							expected_sample = bge.layout_entry.texture.sampleType;
							break;
						}
					}
				}
				if (uniform.ids.size() > 0) {
					WGTexture *tex = (WGTexture *)(uniform.ids[0].id);
					if (tex != nullptr) {
						WGPUBindGroupEntry entry = {};
						entry.binding = uniform.binding * 2;

						// Fix depth/float mismatch: if layout expects Float but
						// texture has a depth format (common with Godot's depth
						// fallback textures), substitute a float fallback texture.
						if (expected_sample == WGPUTextureSampleType_Float &&
								_is_depth_format(tex->format) &&
								fallback_float_texture_view != nullptr) {
							// Also check if we need cube dimension for the depth fallback.
							if (expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr) {
								entry.textureView = fallback_cube_texture_view;
							} else {
								entry.textureView = fallback_float_texture_view;
							}
						} else if (expected_sample == WGPUTextureSampleType_Float &&
								!float32_filterable_supported &&
								_is_float32_format(tex->format) &&
								fallback_float_texture_view != nullptr) {
							// R32Float/RG32Float/RGBA32Float textures are unfilterable
							// without the float32-filterable feature (e.g., Android Chrome
							// on Adreno). Substitute a filterable RGBA8 fallback to avoid
							// validation errors. The texture data is lost but rendering
							// continues without GPU errors.
							if (expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr) {
								entry.textureView = fallback_cube_texture_view;
							} else {
								entry.textureView = fallback_float_texture_view;
							}
						} else if (expected_dim != WGPUTextureViewDimension_Undefined &&
								expected_dim != tex->view_dimension) {
							// Fix dimension mismatch: if the layout expects a different dimension
							// than the texture's default view (e.g., 2D vs Cube), create a
							// compatible view or use a fallback texture.
							//
							// A view's dimension must also be structurally compatible with the
							// GPU texture's own underlying dimension -- WebGPU only allows a 3D
							// view of a 3D-dimension texture (never 2D/Cube/etc.), and only a
							// 2D/2DArray/Cube/CubeArray view of a 2D-dimension texture (never
							// 3D). No amount of view-descriptor trickery can bridge this; found
							// via a real Chrome run where a 4x4x1 texture3D (voxel_gi.glsl's
							// texture_sdf/color_texture, or an sdfgi cascade texture -- a real
							// Godot texture, not a driver-internal fallback) got bound to a
							// generic slot the shader declares as texture_2d<f32>, and the old
							// code below tried creating a 2D view of it regardless, which Dawn
							// rejects outright ("dimension ... not compatible with the
							// dimension ... of [Texture]") rather than erroring more softly.
							// Substitute a fallback instead, same as the Cube/insufficient-
							// layers case just below. See webgpu_notes/TASKS.md Task 9.5 Round 6.
							bool dim_class_incompatible =
									(tex->dimension == WGPUTextureDimension_3D && expected_dim != WGPUTextureViewDimension_3D) ||
									(tex->dimension != WGPUTextureDimension_3D && expected_dim == WGPUTextureViewDimension_3D);
							if (expected_dim == WGPUTextureViewDimension_Cube && tex->layers < 6 &&
									fallback_cube_texture_view != nullptr) {
								// Can't create a cube view from a texture with < 6 layers.
								entry.textureView = fallback_cube_texture_view;
							} else if (dim_class_incompatible && fallback_float_texture_view != nullptr) {
								entry.textureView = (expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr)
										? fallback_cube_texture_view
										: fallback_float_texture_view;
							} else if (tex->view_source != nullptr) {
								// Use slice base offsets so slice views don't
								// silently remap to mip 0 / layer 0 of the parent.
								WGPUTextureViewDescriptor vd = {};
								vd.format = tex->format;
								vd.dimension = expected_dim;
								vd.baseMipLevel = tex->base_mipmap;
								vd.mipLevelCount = tex->mipmaps;
								vd.baseArrayLayer = tex->base_layer;
								vd.arrayLayerCount = (expected_dim == WGPUTextureViewDimension_2D) ? 1 : tex->layers;
								vd.aspect = WGPUTextureAspect_All;
								WGPUTextureView fixed_view = wgpuTextureCreateView(tex->view_source, &vd);
								if (fixed_view) {
									entry.textureView = fixed_view;
									us->temp_views.push_back(fixed_view);
								} else {
									entry.textureView = tex->default_view;
								}
							} else {
								entry.textureView = tex->default_view;
							}
						} else if (expected_sample == WGPUTextureSampleType_Depth &&
								(tex->format == WGPUTextureFormat_Depth24PlusStencil8 ||
										tex->format == WGPUTextureFormat_Depth32FloatStencil8)) {
							// Same fix as the UNIFORM_TYPE_SAMPLER_WITH_TEXTURE case below:
							// a combined depth+stencil format's default view selects both
							// aspects, but a texture_depth_2d binding (Task 7.13) requires
							// a view with ONLY the Depth aspect selected -- WebGPU rejects
							// "Multiple aspects (Depth|Stencil) selected" otherwise. This
							// standalone-texture case (no paired sampler, e.g. a compute
							// shader doing textureLoad on a depth texture directly, as in
							// ClusterDebugShaderRD) had no equivalent fix until now. See
							// webgpu_notes/TASKS.md Task 9.5 Round 5.
							WGPUTextureViewDescriptor vd = {};
							vd.format = (tex->format == WGPUTextureFormat_Depth24PlusStencil8)
									? WGPUTextureFormat_Depth24Plus
									: WGPUTextureFormat_Depth32Float;
							vd.dimension = WGPUTextureViewDimension_2D;
							vd.baseMipLevel = tex->base_mipmap;
							vd.mipLevelCount = tex->mipmaps;
							vd.baseArrayLayer = tex->base_layer;
							vd.arrayLayerCount = 1;
							vd.aspect = WGPUTextureAspect_DepthOnly;
							WGPUTextureView depth_only_view = wgpuTextureCreateView(tex->gpu_handle(), &vd);
							if (depth_only_view) {
								entry.textureView = depth_only_view;
								us->temp_views.push_back(depth_only_view);
							} else {
								entry.textureView = tex->default_view;
							}
						} else {
							entry.textureView = tex->default_view;
						}
						us->bound_textures[entry.binding] = tex;
						entries.push_back(entry);
					}
				}
			} break;

			case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE: {
				// Godot pairs sampler+texture as ids[j*2] / ids[j*2+1].
				// WebGPU needs separate sampler and texture entries.
				// Sampler at binding*2+j*2+0, texture at binding*2+j*2+1 (matches layout and SPIR-V preprocessor) —
				// EXCEPT a multisampled combined sampler (GLSL sampler2DMS), which has no paired sampler
				// variable at all and whose texture sits at binding*2+j*2+0 instead. See the identical
				// ms_no_sampler special-case (and its full explanation) in the BGL-construction copy of
				// this switch, Task 9.5 Round 9.
				WGPUTextureViewDimension swt_expected_dim = WGPUTextureViewDimension_Undefined;
				bool swt_expected_ms = false;
				bool swt_expected_nonfiltering = false;
				WGPUTextureSampleType swt_expected_sample_type = WGPUTextureSampleType_Undefined;
				if (p_set_index < (uint32_t)shader->bind_group_infos.size()) {
					// Try the regular texture slot (binding*2+1) first, then the
					// multisampled-no-sampler slot (binding*2+0) as a fallback.
					uint32_t tex_binding = uniform.binding * 2 + 1;
					uint32_t ms_tex_binding = uniform.binding * 2 + 0;
					const WGShader::BindGroupEntry *found_bge = nullptr;
					for (const auto &bge : shader->bind_group_infos[p_set_index].entries) {
						if (bge.layout_entry.binding == tex_binding) {
							found_bge = &bge;
							break;
						}
					}
					if (!found_bge) {
						for (const auto &bge : shader->bind_group_infos[p_set_index].entries) {
							if (bge.layout_entry.binding == ms_tex_binding && !bge.has_paired_sampler_entry) {
								found_bge = &bge;
								break;
							}
						}
					}
					if (found_bge) {
						swt_expected_dim = found_bge->layout_entry.texture.viewDimension;
						swt_expected_ms = (bool)found_bge->layout_entry.texture.multisampled;
						swt_expected_sample_type = found_bge->layout_entry.texture.sampleType;
						// A combined sampler+texture binding stores only ONE
						// BindGroupEntry (keyed by the texture's binding index)
						// in this reflection structure -- the paired sampler's
						// own layout entry is stashed separately on it (see
						// BindGroupEntry::paired_sampler_entry, Task 7.13) since
						// there's no second entry at samp_binding to scan for.
						if (found_bge->has_paired_sampler_entry &&
								found_bge->paired_sampler_entry.sampler.type == WGPUSamplerBindingType_NonFiltering) {
							swt_expected_nonfiltering = true;
						}
					}
				}
				// ms_no_sampler: this combined binding has no sampler at all (see
				// above) -- only true when the BGL lookup found the texture at the
				// binding*2+0 slot (the tex_binding-first search above only lands
				// there when no regular binding*2+1 entry exists).
				bool swt_ms_no_sampler = swt_expected_ms;
				for (uint32_t j = 0; j < uniform.ids.size() / 2; j++) {
					WGPUSampler sampler = swt_ms_no_sampler ? nullptr : (WGPUSampler)(uniform.ids[j * 2 + 0].id);
					WGTexture *tex = (WGTexture *)(uniform.ids[j * 2 + 1].id);
					if (sampler) {
						WGPUBindGroupEntry se = {};
						se.binding = uniform.binding * 2 + j * 2 + 0;
						// See the analogous substitution in the standalone UNIFORM_TYPE_SAMPLER
						// case above (Task 8.8/8.9) — Godot has no NonFiltering concept and just
						// passes whatever sampler RID it has for this slot.
						se.sampler = (swt_expected_nonfiltering && dummy_nonfiltering_sampler) ? dummy_nonfiltering_sampler : sampler;
						entries.push_back(se);
					}
					if (tex && tex->default_view) {
						WGPUBindGroupEntry te = {};
						te.binding = uniform.binding * 2 + j * 2 + (swt_ms_no_sampler ? 0 : 1);
						// Check sample-count mismatch first: if the BGL expects a multisampled
						// texture but the bound texture isn't multisampled (or vice versa),
						// we can't use either the real texture or a non-MS fallback. Substitute
						// the MSAA fallback for this case. This handles ResolveRasterShaderRD
						// which binds an MSAA depth texture into a float MSAA slot (WebGPU
						// forbids sampling depth as float, so the MSAA fallback is used).
						bool tex_is_ms = (tex->sample_count > 1);
						if (swt_expected_ms && (!tex_is_ms || _is_depth_format(tex->format)) &&
								fallback_ms_texture_view != nullptr) {
							te.textureView = fallback_ms_texture_view;
						} else if (_is_depth_format(tex->format) && swt_expected_sample_type != WGPUTextureSampleType_Depth &&
								fallback_float_texture_view != nullptr) {
							// Fix depth/float mismatch: this combined binding's WGSL type is
							// Float (texture_2d<f32>) but the real texture is depth-format.
							// Substitute a float fallback rather than let Dawn reject binding a
							// depth-format view to a Float-sampleType slot. (When the BGL
							// already expects Depth sampleType here -- see
							// _reclassify_single_component_depth_textures(), Task 7.13 -- this
							// branch is skipped and the real texture's own view is bound
							// directly below, since it's already the correct type.)
							if (swt_expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr) {
								te.textureView = fallback_cube_texture_view;
							} else {
								te.textureView = fallback_float_texture_view;
							}
						} else if (!float32_filterable_supported &&
								_is_float32_format(tex->format) &&
								fallback_float_texture_view != nullptr) {
							// R32Float/RG32Float/RGBA32Float unfilterable without feature.
							if (swt_expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr) {
								te.textureView = fallback_cube_texture_view;
							} else {
								te.textureView = fallback_float_texture_view;
							}
						} else if (swt_expected_dim != WGPUTextureViewDimension_Undefined &&
								swt_expected_dim != tex->view_dimension) {
							// Fix dimension mismatch (e.g., Cube↔2D with fallback textures).
							// See the identical dim_class_incompatible check and its comment
							// in the UNIFORM_TYPE_TEXTURE case above (Task 9.5 Round 6): a
							// view's dimension class must structurally match the real
							// texture's own dimension (3D texture -> 3D view only; 2D texture
							// -> never a 3D view), which no view-descriptor trick can bridge.
							bool swt_dim_class_incompatible =
									(tex->dimension == WGPUTextureDimension_3D && swt_expected_dim != WGPUTextureViewDimension_3D) ||
									(tex->dimension != WGPUTextureDimension_3D && swt_expected_dim == WGPUTextureViewDimension_3D);
							if (swt_expected_dim == WGPUTextureViewDimension_Cube && tex->layers < 6 &&
									fallback_cube_texture_view != nullptr) {
								te.textureView = fallback_cube_texture_view;
							} else if (swt_dim_class_incompatible && fallback_float_texture_view != nullptr) {
								te.textureView = (swt_expected_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr)
										? fallback_cube_texture_view
										: fallback_float_texture_view;
							} else if (tex->view_source != nullptr) {
								// Use slice base offsets so slice views don't
								// silently remap to mip 0 / layer 0 of the parent.
								WGPUTextureViewDescriptor vd = {};
								vd.format = tex->format;
								vd.dimension = swt_expected_dim;
								vd.baseMipLevel = tex->base_mipmap;
								vd.mipLevelCount = tex->mipmaps;
								vd.baseArrayLayer = tex->base_layer;
								vd.arrayLayerCount = (swt_expected_dim == WGPUTextureViewDimension_2D) ? 1 : tex->layers;
								vd.aspect = WGPUTextureAspect_All;
								WGPUTextureView fixed_view = wgpuTextureCreateView(tex->view_source, &vd);
								if (fixed_view) {
									te.textureView = fixed_view;
									us->temp_views.push_back(fixed_view);
								} else {
									te.textureView = tex->default_view;
								}
							} else {
								te.textureView = tex->default_view;
							}
						} else if (swt_expected_sample_type == WGPUTextureSampleType_Depth &&
								(tex->format == WGPUTextureFormat_Depth24PlusStencil8 ||
										tex->format == WGPUTextureFormat_Depth32FloatStencil8)) {
							// A combined depth+stencil format's default view selects
							// both aspects, but a texture_depth_2d binding (Task 7.13)
							// requires a view with ONLY the Depth aspect selected --
							// WebGPU rejects "Multiple aspects (Depth|Stencil)
							// selected" otherwise. Create a depth-only view on demand.
							// A DepthOnly-aspect view of a combined format must also
							// declare its own format as the depth-only equivalent, not
							// the packed combined format (WebGPU rejects "view format
							// is not compatible with TextureAspect::DepthOnly" if
							// left as e.g. Depth24PlusStencil8 itself).
							WGPUTextureViewDescriptor vd = {};
							vd.format = (tex->format == WGPUTextureFormat_Depth24PlusStencil8)
									? WGPUTextureFormat_Depth24Plus
									: WGPUTextureFormat_Depth32Float;
							vd.dimension = WGPUTextureViewDimension_2D;
							vd.baseMipLevel = tex->base_mipmap;
							vd.mipLevelCount = tex->mipmaps;
							vd.baseArrayLayer = tex->base_layer;
							vd.arrayLayerCount = 1;
							vd.aspect = WGPUTextureAspect_DepthOnly;
							WGPUTextureView depth_only_view = wgpuTextureCreateView(tex->gpu_handle(), &vd);
							if (depth_only_view) {
								te.textureView = depth_only_view;
								us->temp_views.push_back(depth_only_view);
							} else {
								te.textureView = tex->default_view;
							}
						} else {
							te.textureView = tex->default_view;
						}
						us->bound_textures[te.binding] = tex;
						entries.push_back(te);
					}
				}
			} break;

			case UNIFORM_TYPE_IMAGE: {
				// Tint flattens binding arrays to single resources.
				// Only provide the first image for array bindings.
				if (uniform.ids.size() > 0) {
					WGTexture *tex = (WGTexture *)(uniform.ids[0].id);
					if (tex != nullptr) {
						WGPUBindGroupEntry entry = {};
						entry.binding = uniform.binding * 2;
						entry.textureView = tex->default_view;
						us->bound_textures[entry.binding] = tex;
						entries.push_back(entry);
					}
				}
			} break;

			case UNIFORM_TYPE_UNIFORM_BUFFER: {
				WGBuffer *buf = (WGBuffer *)(uniform.ids[0].id);
				ERR_CONTINUE_MSG(buf == nullptr, "WebGPU: null buffer in uniform set.");
				WGPUBindGroupEntry entry = {};
				entry.binding = uniform.binding * 2; // Preprocessing doubles all non-combined bindings.
				entry.buffer = buf->handle;
				entry.offset = 0;
				entry.size = buf->size;
				entries.push_back(entry);
			} break;

			case UNIFORM_TYPE_STORAGE_BUFFER: {
				WGBuffer *buf = (WGBuffer *)(uniform.ids[0].id);
				ERR_CONTINUE_MSG(buf == nullptr, "WebGPU: null buffer in storage uniform.");
				WGPUBindGroupEntry entry = {};
				entry.binding = uniform.binding * 2; // Preprocessing doubles all non-combined bindings.
				entry.offset = 0;
				// Alias detection: if this exact WGPUBuffer handle was already added as a
				// storage binding in this set, redirect the duplicate to the stub buffer.
				// This avoids the WebGPU "writable storage buffer aliasing" validation error
				// that fires when e.g. the particle system passes the same buffer for both
				// SourceEmission (binding 2) and DestEmission (binding 3).
				if (aliasing_stub_buffer && dup_storage_seen.has(buf->handle)) {
					static bool alias_stub_logged = false;
					if (!alias_stub_logged) {
						alias_stub_logged = true;
						WEBGPU_DIAG({ console.warn('[ALIAS-STUB] Writable storage buffer aliasing at set=' + $0 + ' binding=' + $1 + ', redirected to stub buffer'); }, (int)p_set_index, (int)uniform.binding);
					}
					entry.buffer = aliasing_stub_buffer;
					entry.size = ALIASING_STUB_BUFFER_SIZE;
				} else {
					dup_storage_seen[buf->handle] = i;
					entry.buffer = buf->handle;
					entry.size = buf->size;
				}
				entries.push_back(entry);
			} break;

			case UNIFORM_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case UNIFORM_TYPE_STORAGE_BUFFER_DYNAMIC: {
				WGBuffer *buf = (WGBuffer *)(uniform.ids[0].id);
				ERR_CONTINUE_MSG(buf == nullptr, "WebGPU: null buffer in dynamic uniform.");
				WGPUBindGroupEntry entry = {};
				entry.binding = uniform.binding * 2;
				entry.buffer = buf->handle;
				entry.offset = 0;
				// Task 7.5: For dynamic persistent buffers, bind ONE slice (per_frame_size),
				// not the full physical allocation. The rest of the buffer holds the other
				// frames' slices, reached via the dynamic offset at bind time.
				if (buf->is_dynamic() && buf->per_frame_size > 0) {
					entry.size = buf->per_frame_size;
					us->dynamic_buffers.push_back(buf);
				} else {
					entry.size = buf->size;
				}
				entries.push_back(entry);
			} break;

			case UNIFORM_TYPE_TEXTURE_BUFFER:
			case UNIFORM_TYPE_SAMPLER_WITH_TEXTURE_BUFFER: {
				// Tint converts texture buffers to uniform/storage buffers. Bind as buffer.
				WGBuffer *buf = (WGBuffer *)(uniform.ids[0].id);
				ERR_CONTINUE_MSG(buf == nullptr, "WebGPU: null buffer in texture buffer uniform.");
				WGPUBindGroupEntry entry = {};
				entry.binding = uniform.binding * 2;
				entry.buffer = buf->handle;
				entry.offset = 0;
				entry.size = buf->size;
				entries.push_back(entry);
			} break;

			case UNIFORM_TYPE_IMAGE_BUFFER: {
				WGBuffer *buf = (WGBuffer *)(uniform.ids[0].id);
				ERR_CONTINUE_MSG(buf == nullptr, "WebGPU: null buffer in image buffer uniform.");
				WGPUBindGroupEntry entry = {};
				entry.binding = uniform.binding * 2;
				entry.buffer = buf->handle;
				entry.offset = 0;
				entry.size = buf->size;
				entries.push_back(entry);
			} break;

			default: {
				WARN_PRINT_ONCE(vformat("WebGPU: unhandled uniform type %d in uniform_set_create.", (int)uniform.type));
			} break;
		}
	}

	// Add depth alias bind group entries: for each depth alias binding, provide
	// the same texture view as the paired depth texture at (alias_binding - 1).
	for (const KeyValue<uint32_t, uint32_t> &kv : shader->depth_alias_bindings) {
		uint32_t alias_grp = kv.key >> 16;
		uint32_t alias_bnd = kv.key & 0xFFFF;
		uint32_t depth_bnd = kv.value;
		if (alias_grp != p_set_index) {
			continue;
		}
		// Find the texture view from the depth texture entry.
		WGPUTextureView alias_view = nullptr;
		for (const auto &e : entries) {
			if (e.binding == depth_bnd && e.textureView != nullptr) {
				alias_view = e.textureView;
				break;
			}
		}
		if (alias_view) {
			WGPUBindGroupEntry alias_entry = {};
			alias_entry.binding = alias_bnd;
			// The alias is supposed to sample depth as Float, but alias_view may
			// point at a Depth-format texture. Substitute the float fallback.
			if (alias_view == fallback_float_texture_view) {
				alias_entry.textureView = alias_view; // Already substituted.
			} else {
				// Check if the source texture view came from a depth-format texture.
				// We can't query the view's format, so check if the alias BGL expects Float.
				// For safety, always use fallback for depth alias entries since
				// they always represent Float sampling of depth data.
				if (fallback_float_texture_view != nullptr) {
					alias_entry.textureView = fallback_float_texture_view;
				} else {
					alias_entry.textureView = alias_view;
				}
			}
			entries.push_back(alias_entry);
		}
	}

	// Add shadow read bindings for read_write storage texture splits.
	// For each split, create a GPU copy of the original texture and bind it
	// at the shadow read slot. See _create_rw_shadow_bind_entry()'s doc
	// comment for why this is factored into a shared helper (Task 9.5 Round 24).
	for (const KeyValue<uint32_t, uint32_t> &kv : shader->rw_storage_splits) {
		uint32_t split_grp = kv.key >> 16;
		uint32_t write_bnd = kv.key & 0xFFFF;
		uint32_t shadow_bnd = kv.value;
		if (split_grp != p_set_index) {
			continue;
		}
		// Find the original texture from the write binding.
		WGTexture *orig_tex = nullptr;
		if (us->bound_textures.has(write_bnd)) {
			orig_tex = us->bound_textures[write_bnd];
		}
		if (!orig_tex) {
			continue;
		}
		WGPUBindGroupEntry shadow_entry;
		if (_create_rw_shadow_bind_entry(us, orig_tex, shadow_bnd, shadow_entry)) {
			entries.push_back(shadow_entry);
		}
	}

	// If this is the merged PC group, inject the push constant ring buffer at PUSH_CONSTANT_RING_BINDING.
	// Dynamic offset is 0 here; _flush_push_constants will rebind with the correct offset.
	if (is_pc_group) {
		WGPUBindGroupEntry pc_entry = {};
		pc_entry.binding = PUSH_CONSTANT_RING_BINDING;
		pc_entry.buffer = push_constant_ring_buffer;
		pc_entry.offset = 0;
		pc_entry.size = PUSH_CONSTANT_SLOT_ALIGNMENT;
		entries.push_back(pc_entry);
	}

	// Log any duplicate binding indices in bind group entries.
	for (uint32_t a = 0; a < entries.size(); a++) {
		for (uint32_t b = a + 1; b < entries.size(); b++) {
			if (entries[a].binding == entries[b].binding) {
				WEBGPU_DIAG({ console.error('[BG-DUP] set=' + $0 + ' binding=' + $1 + ' idx_a=' + $2 + ' idx_b=' + $3); }, (int)p_set_index, (int)entries[a].binding, (int)a, (int)b);
			}
		}
	}

	WGPUBindGroupDescriptor bg_desc = {};
	bg_desc.layout = layout;
	bg_desc.entryCount = entries.size();
	bg_desc.entries = entries.size() > 0 ? entries.ptr() : nullptr;

	WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &bg_desc);
	if (bg == nullptr) {
		delete us;
		ERR_FAIL_V_MSG(UniformSetID(), "WebGPU: wgpuDeviceCreateBindGroup failed.");
	}

	us->handle = bg;
	// Cache entries and source shader for potential BGL rebinding.
	us->cached_entries.resize(entries.size());
	for (uint32_t i = 0; i < entries.size(); i++) {
		us->cached_entries[i] = entries[i];
	}
	us->source_shader = shader;
	return UniformSetID(us);
}

WGPUBindGroup RenderingDeviceDriverWebGPU::_get_compatible_bind_group(WGUniformSet *p_us, WGShader *p_target_shader, uint32_t p_set_idx) {
	// Fast path: same shader or no shader info.
	if (!p_target_shader || p_us->source_shader == p_target_shader) {
		return p_us->handle;
	}
	if (p_set_idx >= (uint32_t)p_target_shader->bind_group_layouts.size()) {
		return p_us->handle;
	}

	// Check if the pipeline shader uses the same BGL or a merged PC layout.
	WGPUBindGroupLayout target_layout = p_target_shader->bind_group_layouts[p_set_idx];
	if (p_target_shader->merged_pc_group_layout && p_set_idx == p_target_shader->push_constant_bind_group) {
		target_layout = p_target_shader->merged_pc_group_layout;
	}

	// If source shader uses the same BGL, no rebind needed.
	WGPUBindGroupLayout source_layout = nullptr;
	if (p_us->source_shader && p_set_idx < (uint32_t)p_us->source_shader->bind_group_layouts.size()) {
		source_layout = p_us->source_shader->bind_group_layouts[p_set_idx];
		if (p_us->source_shader->merged_pc_group_layout && p_set_idx == p_us->source_shader->push_constant_bind_group) {
			source_layout = p_us->source_shader->merged_pc_group_layout;
		}
	}
	if (source_layout == target_layout) {
		return p_us->handle;
	}

	// Check rebind cache.
	if (p_us->rebind_cache.has(target_layout)) {
		return p_us->rebind_cache[target_layout];
	}

	// Build adapted entries: copy cached entries and fix sampler type mismatches.
	LocalVector<WGPUBindGroupEntry> adapted;
	adapted.resize(p_us->cached_entries.size());
	for (uint32_t i = 0; i < p_us->cached_entries.size(); i++) {
		adapted[i] = p_us->cached_entries[i];
	}

	// Check sampler and texture entries for type mismatches between source and target BGLs.
	if (p_set_idx < (uint32_t)p_target_shader->bind_group_infos.size()) {
		for (auto &entry : adapted) {
			// --- Sampler adaptation ---
			if (entry.sampler != nullptr) {
				WGPUSamplerBindingType target_samp_type = WGPUSamplerBindingType_BindingNotUsed;
				for (const auto &bge : p_target_shader->bind_group_infos[p_set_idx].entries) {
					if (bge.layout_entry.binding == entry.binding &&
							bge.layout_entry.sampler.type != WGPUSamplerBindingType_BindingNotUsed) {
						target_samp_type = bge.layout_entry.sampler.type;
						break;
					}
				}
				// Fallback: check source shader for SWT sampler info.
				if (target_samp_type == WGPUSamplerBindingType_BindingNotUsed &&
						p_us->source_shader && p_set_idx < (uint32_t)p_us->source_shader->bind_group_infos.size()) {
					for (const auto &bge : p_us->source_shader->bind_group_infos[p_set_idx].entries) {
						if (bge.layout_entry.binding == entry.binding &&
								bge.layout_entry.sampler.type != WGPUSamplerBindingType_BindingNotUsed) {
							target_samp_type = bge.layout_entry.sampler.type;
							break;
						}
					}
				}

				WGPUSamplerBindingType source_samp_type = WGPUSamplerBindingType_BindingNotUsed;
				if (p_us->source_shader && p_set_idx < (uint32_t)p_us->source_shader->bind_group_infos.size()) {
					for (const auto &bge : p_us->source_shader->bind_group_infos[p_set_idx].entries) {
						if (bge.layout_entry.binding == entry.binding &&
								bge.layout_entry.sampler.type != WGPUSamplerBindingType_BindingNotUsed) {
							source_samp_type = bge.layout_entry.sampler.type;
							break;
						}
					}
				}

				if ((target_samp_type == WGPUSamplerBindingType_Filtering ||
							target_samp_type == WGPUSamplerBindingType_NonFiltering) &&
						source_samp_type == WGPUSamplerBindingType_Comparison && dummy_filtering_sampler) {
					entry.sampler = dummy_filtering_sampler;
				} else if (target_samp_type == WGPUSamplerBindingType_Comparison &&
						source_samp_type != WGPUSamplerBindingType_Comparison && dummy_comparison_sampler) {
					entry.sampler = dummy_comparison_sampler;
				}
			}

			// --- Texture view adaptation ---
			// If the target BGL expects Float but the entry has a depth-format texture view,
			// substitute the fallback float texture view.
			if (entry.textureView != nullptr) {
				WGPUTextureSampleType target_tex_sample = WGPUTextureSampleType_BindingNotUsed;
				for (const auto &bge : p_target_shader->bind_group_infos[p_set_idx].entries) {
					if (bge.layout_entry.binding == entry.binding &&
							bge.layout_entry.texture.sampleType != WGPUTextureSampleType_BindingNotUsed) {
						target_tex_sample = bge.layout_entry.texture.sampleType;
						break;
					}
				}
				// Check source BGL to detect if the texture was originally depth.
				WGPUTextureSampleType source_tex_sample = WGPUTextureSampleType_BindingNotUsed;
				if (p_us->source_shader && p_set_idx < (uint32_t)p_us->source_shader->bind_group_infos.size()) {
					for (const auto &bge : p_us->source_shader->bind_group_infos[p_set_idx].entries) {
						if (bge.layout_entry.binding == entry.binding &&
								bge.layout_entry.texture.sampleType != WGPUTextureSampleType_BindingNotUsed) {
							source_tex_sample = bge.layout_entry.texture.sampleType;
							break;
						}
					}
				}
				// Source was Depth, target expects Float → substitute fallback.
				if (source_tex_sample == WGPUTextureSampleType_Depth &&
						target_tex_sample == WGPUTextureSampleType_Float &&
						fallback_float_texture_view != nullptr) {
					entry.textureView = fallback_float_texture_view;
				}

				// --- Texture view dimension adaptation ---
				// If the target BGL expects a different dimension (e.g. 2D vs Cube),
				// create a compatible view from the original texture.
				WGPUTextureViewDimension target_dim = WGPUTextureViewDimension_Undefined;
				for (const auto &bge : p_target_shader->bind_group_infos[p_set_idx].entries) {
					if (bge.layout_entry.binding == entry.binding &&
							bge.layout_entry.texture.viewDimension != WGPUTextureViewDimension_Undefined) {
						target_dim = bge.layout_entry.texture.viewDimension;
						break;
					}
				}
				if (target_dim != WGPUTextureViewDimension_Undefined &&
						p_us->bound_textures.has(entry.binding)) {
					WGTexture *tex = p_us->bound_textures[entry.binding];
					if (tex && tex->view_dimension != target_dim && tex->view_source) {
						// A view's dimension must structurally match the real texture's
						// own dimension (3D texture -> 3D view only; 2D texture -> never
						// a 3D view) -- the same check applied in the general uniform-
						// binding fixups (Task 9.5 Round 6) was missing here, in this
						// separate "adapt a cached uniform set to a different but
						// layout-compatible target shader" path. Found via a real Chrome
						// run + a debug probe against scene_forward_clustered.glsl's
						// voxel_gi_textures/sdfgi_occlusion_cascades/volumetric_fog_texture
						// (all texture_3d<f32>, every variant checked agreed on that
						// dimension) -- the mismatch this function reacts to must be
						// coming from two *different* shader variants disagreeing on
						// declared dimension for the same binding number in some case not
						// caught by that check, which this substitution avoids crashing on
						// regardless of the exact trigger. See webgpu_notes/TASKS.md Task
						// 9.5 Round 11.
						bool dim_class_incompatible =
								(tex->dimension == WGPUTextureDimension_3D && target_dim != WGPUTextureViewDimension_3D) ||
								(tex->dimension != WGPUTextureDimension_3D && target_dim == WGPUTextureViewDimension_3D);
						if (dim_class_incompatible) {
							if (target_dim == WGPUTextureViewDimension_Cube && fallback_cube_texture_view != nullptr) {
								entry.textureView = fallback_cube_texture_view;
							} else if (fallback_float_texture_view != nullptr) {
								entry.textureView = fallback_float_texture_view;
							}
						} else {
							// Use slice base offsets so slice views don't
							// silently remap to mip 0 / layer 0 of the parent.
							WGPUTextureViewDescriptor vd = {};
							vd.format = tex->format;
							vd.dimension = target_dim;
							vd.baseMipLevel = tex->base_mipmap;
							vd.mipLevelCount = tex->mipmaps;
							vd.baseArrayLayer = tex->base_layer;
							vd.arrayLayerCount = (target_dim == WGPUTextureViewDimension_2D) ? 1 : tex->layers;
							vd.aspect = WGPUTextureAspect_All;
							WGPUTextureView fixed = wgpuTextureCreateView(tex->view_source, &vd);
							if (fixed) {
								entry.textureView = fixed;
								p_us->temp_views.push_back(fixed);
							}
						}
					}
				}
			}
		}
	}

	// Collect valid binding indices from the target BGL.
	// The target BGL includes entries from bind_group_infos (reflecting Godot uniforms)
	// plus depth alias entries and potentially a push constant entry.
	HashSet<uint32_t> target_bindings;
	if (p_set_idx < (uint32_t)p_target_shader->bind_group_infos.size()) {
		for (const auto &bge : p_target_shader->bind_group_infos[p_set_idx].entries) {
			target_bindings.insert(bge.layout_entry.binding);
			// For SWT, the layout_entry stores the texture binding; the sampler is at binding-1.
			if (bge.godot_type == RDD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE) {
				target_bindings.insert(bge.layout_entry.binding - 1); // Sampler binding.
			}
		}
	}
	// Add depth alias bindings.
	for (const KeyValue<uint32_t, uint32_t> &kv : p_target_shader->depth_alias_bindings) {
		uint32_t alias_grp = kv.key >> 16;
		uint32_t alias_bnd = kv.key & 0xFFFF;
		if (alias_grp == p_set_idx) {
			target_bindings.insert(alias_bnd);
		}
	}
	// Add rw_storage shadow read bindings.
	for (const KeyValue<uint32_t, uint32_t> &kv : p_target_shader->rw_storage_splits) {
		uint32_t split_grp = kv.key >> 16;
		if (split_grp == p_set_idx) {
			target_bindings.insert(kv.value);
		}
	}
	// Add push constant ring buffer binding if this is the PC group.
	if (p_target_shader->merged_pc_group_layout && p_set_idx == p_target_shader->push_constant_bind_group) {
		target_bindings.insert(PUSH_CONSTANT_RING_BINDING);
	}

	// Filter adapted entries: only keep entries whose binding exists in the target BGL.
	LocalVector<WGPUBindGroupEntry> filtered;
	for (const auto &e : adapted) {
		if (target_bindings.has(e.binding)) {
			filtered.push_back(e);
		}
	}

	// The target shader may need a read_write-storage shadow entry the
	// source shader never created -- two ShaderRD mode variants sharing the
	// same uniform interface can disagree on which bindings are genuinely
	// read_write in their own DCE'd WGSL (it depends on which #ifdef-gated
	// code paths a given mode actually reaches, not just the binding
	// number), so p_us->cached_entries (built for the *source* shader) can
	// be missing a shadow the *target* shader's own rw_storage_splits map
	// requires. Filtering above can only ever remove a stale entry the
	// target doesn't want -- it can't invent the one the target does want
	// but the source never built, which silently produced a bind group with
	// one fewer entry than the target layout expects ("Number of entries
	// (N-1) did not match the expected number of entries (N)"). Task 9.5
	// Round 24 -- first hit by SdfgiIntegrateShaderRD, where mode 0 never
	// needed a shadow for lightprobe_texture_data (binding 16) but mode 1 did.
	for (const KeyValue<uint32_t, uint32_t> &kv : p_target_shader->rw_storage_splits) {
		uint32_t split_grp = kv.key >> 16;
		uint32_t write_bnd = kv.key & 0xFFFF;
		uint32_t shadow_bnd = kv.value;
		if (split_grp != p_set_idx) {
			continue;
		}
		bool already_have = false;
		for (const auto &e : filtered) {
			if (e.binding == shadow_bnd) {
				already_have = true;
				break;
			}
		}
		if (already_have) {
			continue;
		}
		WGTexture *orig_tex = p_us->bound_textures.has(write_bnd) ? p_us->bound_textures[write_bnd] : nullptr;
		if (!orig_tex) {
			continue;
		}
		WGPUBindGroupEntry shadow_entry;
		if (_create_rw_shadow_bind_entry(p_us, orig_tex, shadow_bnd, shadow_entry)) {
			filtered.push_back(shadow_entry);
		}
	}

	// Create re-bound bind group with target layout.
	perf.bind_group_cache_misses++;
	WGPUBindGroupDescriptor desc = {};
	desc.layout = target_layout;
	desc.entryCount = filtered.size();
	desc.entries = filtered.size() > 0 ? filtered.ptr() : nullptr;
	WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &desc);
	if (!bg) {
		WARN_PRINT(vformat("WebGPU: bind group rebind failed for set=%d src=%s tgt=%s entries=%d",
				(int)p_set_idx,
				p_us->source_shader ? p_us->source_shader->name : String("null"),
				p_target_shader->name,
				(int)filtered.size()));
	}
	p_us->rebind_cache[target_layout] = bg; // Cache even if null.
	return bg ? bg : p_us->handle;
}

void RenderingDeviceDriverWebGPU::uniform_set_free(UniformSetID p_uniform_set) {
	WGUniformSet *us = (WGUniformSet *)(p_uniform_set.id);
	ERR_FAIL_NULL(us);
	for (WGPUTextureView v : us->temp_views) {
		if (v) {
			wgpuTextureViewRelease(v);
		}
	}
	// Release shadow read textures/views from read_write storage splits.
	for (WGPUTextureView v : us->rw_shadow_views) {
		if (v) {
			wgpuTextureViewRelease(v);
		}
	}
	for (WGPUTexture t : us->rw_shadow_textures) {
		if (t) {
			wgpuTextureRelease(t);
		}
	}
	// Deregister shadow copy targets from the driver-level map.
	for (const WGUniformSet::RWShadowRegistration &reg : us->rw_shadow_registrations) {
		if (rw_shadow_copy_map.has(reg.source)) {
			LocalVector<WGPUTexture> &shadows = rw_shadow_copy_map[reg.source];
			for (int i = (int)shadows.size() - 1; i >= 0; i--) {
				if (shadows[i] == reg.shadow) {
					shadows.remove_at(i);
					break;
				}
			}
			if (shadows.is_empty()) {
				rw_shadow_copy_map.erase(reg.source);
			}
		}
	}
	// Release cached rebind bind groups.
	for (const KeyValue<WGPUBindGroupLayout, WGPUBindGroup> &kv : us->rebind_cache) {
		if (kv.value) {
			wgpuBindGroupRelease(kv.value);
		}
	}
	if (us->handle) {
		wgpuBindGroupRelease(us->handle);
	}
	delete us;
}

uint32_t RenderingDeviceDriverWebGPU::uniform_sets_get_dynamic_offsets(VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count) const {
	// Task 7.5: Pack frame_idx for every dynamic buffer across the given sets into
	// 4-bit slots of a single uint32_t mask. Matches the Vulkan driver's encoding —
	// command_bind_*_uniform_sets reads the same mask and unpacks (frame_idx & 0xF).
	uint32_t mask = 0;
	uint32_t shift = 0;
	for (uint32_t i = 0; i < p_set_count; i++) {
		const WGUniformSet *us = (const WGUniformSet *)p_uniform_sets[i].id;
		if (!us) {
			continue;
		}
		for (const WGBuffer *buf : us->dynamic_buffers) {
			if (!buf) {
				continue;
			}
			mask |= (buf->frame_idx & 0xFu) << shift;
			shift += 4u;
		}
	}
	return mask;
}

void RenderingDeviceDriverWebGPU::command_uniform_set_prepare_for_use(CommandBufferID p_cmd_buffer, UniformSetID p_uniform_set, ShaderID p_shader, uint32_t p_set_index) {
	// No-op: WebGPU doesn't need explicit preparation.
}

// =============================================================================
// TRANSFER
// =============================================================================

void RenderingDeviceDriverWebGPU::command_clear_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, uint64_t p_offset, uint64_t p_size) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(buf);

	cmd->end_active_encoder();

	uint64_t size = (p_size == BUFFER_WHOLE_SIZE) ? (buf->size - p_offset) : p_size;
	size = (size + 3) & ~3ULL; // Must be multiple of 4.
	wgpuCommandEncoderClearBuffer(cmd->encoder, buf->handle, p_offset, size);
}

void RenderingDeviceDriverWebGPU::command_copy_buffer(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, BufferID p_dst_buffer, VectorView<BufferCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *src = (WGBuffer *)(p_src_buffer.id);
	WGBuffer *dst = (WGBuffer *)(p_dst_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	// If the source buffer has a shadow map (CPU-side staging buffer), flush
	// the shadow map to the staging buffer's GPU handle first via the queue,
	// then use the command encoder to copy staging→dst.  We must NOT write
	// directly to dst via wgpuQueueWriteBuffer because writeBuffer calls are
	// NOT ordered relative to draw commands within the same submission — they
	// all take effect before any draw, so every canvas in the frame would see
	// the last-written value rather than the per-canvas value.  Using the
	// encoder copy preserves the staging→dst copy order relative to draws.
	if (src->shadow_map) {
		for (uint32_t i = 0; i < p_regions.size(); i++) {
			const BufferCopyRegion &region = p_regions[i];
			uint64_t size = (region.size + 3) & ~3ULL;
			wgpuQueueWriteBuffer(queue, src->handle, region.src_offset, src->shadow_map + region.src_offset, size);
		}
		// The specific regions have been flushed above. Clear map_dirty so the
		// subsequent buffer_unmap() doesn't redundantly flush the entire buffer.
		src->map_dirty = false;
		// Fall through to encoder copy below.
	}

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferCopyRegion &region = p_regions[i];
		uint64_t size = (region.size + 3) & ~3ULL;
		wgpuCommandEncoderCopyBufferToBuffer(cmd->encoder, src->handle, region.src_offset, dst->handle, region.dst_offset, size);
	}
}

void RenderingDeviceDriverWebGPU::command_copy_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<TextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *src = (WGTexture *)(p_src_texture.id);
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const TextureCopyRegion &region = p_regions[i];

		WGPUTexelCopyTextureInfo src_copy = {};
		src_copy.texture = src->gpu_handle();
		src_copy.mipLevel = region.src_subresources.mipmap;
		src_copy.origin = { (uint32_t)region.src_offset.x, (uint32_t)region.src_offset.y, region.src_subresources.base_layer };
		src_copy.aspect = WGPUTextureAspect_All;

		WGPUTexelCopyTextureInfo dst_copy = {};
		dst_copy.texture = dst->gpu_handle();
		dst_copy.mipLevel = region.dst_subresources.mipmap;
		dst_copy.origin = { (uint32_t)region.dst_offset.x, (uint32_t)region.dst_offset.y, region.dst_subresources.base_layer };
		dst_copy.aspect = WGPUTextureAspect_All;

		WGPUExtent3D extent = { (uint32_t)region.size.x, (uint32_t)region.size.y, (uint32_t)region.size.z };

		wgpuCommandEncoderCopyTextureToTexture(cmd->encoder, &src_copy, &dst_copy, &extent);
	}
}

void RenderingDeviceDriverWebGPU::command_resolve_texture(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, uint32_t p_src_layer, uint32_t p_src_mipmap, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, uint32_t p_dst_layer, uint32_t p_dst_mipmap) {
	// TODO: Create a minimal render pass with MSAA texture as color attachment
	// and the resolve target as resolveTarget. Begin and immediately end the pass.
	WARN_PRINT_ONCE("WebGPU: command_resolve_texture not yet implemented.");
}

// Return the byte size of a single texel for a given WGPUTextureFormat.
// Returns 0 for compressed or unknown formats.
static uint32_t _wgpu_format_byte_size(WGPUTextureFormat p_format) {
	switch (p_format) {
		case WGPUTextureFormat_R8Unorm:
		case WGPUTextureFormat_R8Snorm:
		case WGPUTextureFormat_R8Uint:
		case WGPUTextureFormat_R8Sint:
			return 1;
		case WGPUTextureFormat_R16Uint:
		case WGPUTextureFormat_R16Sint:
		case WGPUTextureFormat_R16Float:
		case WGPUTextureFormat_RG8Unorm:
		case WGPUTextureFormat_RG8Snorm:
		case WGPUTextureFormat_RG8Uint:
		case WGPUTextureFormat_RG8Sint:
			return 2;
		case WGPUTextureFormat_R32Float:
		case WGPUTextureFormat_R32Uint:
		case WGPUTextureFormat_R32Sint:
		case WGPUTextureFormat_RG16Uint:
		case WGPUTextureFormat_RG16Sint:
		case WGPUTextureFormat_RG16Float:
		case WGPUTextureFormat_RGBA8Unorm:
		case WGPUTextureFormat_RGBA8UnormSrgb:
		case WGPUTextureFormat_RGBA8Snorm:
		case WGPUTextureFormat_RGBA8Uint:
		case WGPUTextureFormat_RGBA8Sint:
		case WGPUTextureFormat_BGRA8Unorm:
		case WGPUTextureFormat_BGRA8UnormSrgb:
		case WGPUTextureFormat_RGB10A2Unorm:
		case WGPUTextureFormat_RG11B10Ufloat:
			return 4;
		case WGPUTextureFormat_RG32Float:
		case WGPUTextureFormat_RG32Uint:
		case WGPUTextureFormat_RG32Sint:
		case WGPUTextureFormat_RGBA16Uint:
		case WGPUTextureFormat_RGBA16Sint:
		case WGPUTextureFormat_RGBA16Float:
			return 8;
		case WGPUTextureFormat_RGBA32Float:
		case WGPUTextureFormat_RGBA32Uint:
		case WGPUTextureFormat_RGBA32Sint:
			return 16;
		default:
			return 0;
	}
}

// Encode a clear Color into the raw texel bytes for a given WGPUTextureFormat.
// r_texel must point to at least 16 bytes and be pre-zeroed.
static void _encode_clear_texel(WGPUTextureFormat p_format, const Color &p_color, uint8_t *r_texel) {
	switch (p_format) {
		// Float32 formats.
		case WGPUTextureFormat_R32Float: {
			float v = p_color.r;
			memcpy(r_texel, &v, 4);
		} break;
		case WGPUTextureFormat_RG32Float: {
			float v[2] = { p_color.r, p_color.g };
			memcpy(r_texel, v, 8);
		} break;
		case WGPUTextureFormat_RGBA32Float: {
			float v[4] = { p_color.r, p_color.g, p_color.b, p_color.a };
			memcpy(r_texel, v, 16);
		} break;

		// Uint32 formats.
		case WGPUTextureFormat_R32Uint: {
			uint32_t v = (uint32_t)p_color.r;
			memcpy(r_texel, &v, 4);
		} break;
		case WGPUTextureFormat_RG32Uint: {
			uint32_t v[2] = { (uint32_t)p_color.r, (uint32_t)p_color.g };
			memcpy(r_texel, v, 8);
		} break;
		case WGPUTextureFormat_RGBA32Uint: {
			uint32_t v[4] = { (uint32_t)p_color.r, (uint32_t)p_color.g, (uint32_t)p_color.b, (uint32_t)p_color.a };
			memcpy(r_texel, v, 16);
		} break;

		// Sint32 formats.
		case WGPUTextureFormat_R32Sint: {
			int32_t v = (int32_t)p_color.r;
			memcpy(r_texel, &v, 4);
		} break;
		case WGPUTextureFormat_RG32Sint: {
			int32_t v[2] = { (int32_t)p_color.r, (int32_t)p_color.g };
			memcpy(r_texel, v, 8);
		} break;
		case WGPUTextureFormat_RGBA32Sint: {
			int32_t v[4] = { (int32_t)p_color.r, (int32_t)p_color.g, (int32_t)p_color.b, (int32_t)p_color.a };
			memcpy(r_texel, v, 16);
		} break;

		// Unorm8 formats.
		case WGPUTextureFormat_R8Unorm: {
			r_texel[0] = (uint8_t)CLAMP(p_color.r * 255.0f, 0.0f, 255.0f);
		} break;
		case WGPUTextureFormat_RG8Unorm: {
			r_texel[0] = (uint8_t)CLAMP(p_color.r * 255.0f, 0.0f, 255.0f);
			r_texel[1] = (uint8_t)CLAMP(p_color.g * 255.0f, 0.0f, 255.0f);
		} break;
		case WGPUTextureFormat_RGBA8Unorm:
		case WGPUTextureFormat_RGBA8UnormSrgb: {
			r_texel[0] = (uint8_t)CLAMP(p_color.r * 255.0f, 0.0f, 255.0f);
			r_texel[1] = (uint8_t)CLAMP(p_color.g * 255.0f, 0.0f, 255.0f);
			r_texel[2] = (uint8_t)CLAMP(p_color.b * 255.0f, 0.0f, 255.0f);
			r_texel[3] = (uint8_t)CLAMP(p_color.a * 255.0f, 0.0f, 255.0f);
		} break;
		case WGPUTextureFormat_BGRA8Unorm:
		case WGPUTextureFormat_BGRA8UnormSrgb: {
			r_texel[0] = (uint8_t)CLAMP(p_color.b * 255.0f, 0.0f, 255.0f);
			r_texel[1] = (uint8_t)CLAMP(p_color.g * 255.0f, 0.0f, 255.0f);
			r_texel[2] = (uint8_t)CLAMP(p_color.r * 255.0f, 0.0f, 255.0f);
			r_texel[3] = (uint8_t)CLAMP(p_color.a * 255.0f, 0.0f, 255.0f);
		} break;

		// Float16 formats.
		case WGPUTextureFormat_R16Float: {
			uint16_t v = Math::make_half_float(p_color.r);
			memcpy(r_texel, &v, 2);
		} break;
		case WGPUTextureFormat_RG16Float: {
			uint16_t v[2] = { Math::make_half_float(p_color.r), Math::make_half_float(p_color.g) };
			memcpy(r_texel, v, 4);
		} break;
		case WGPUTextureFormat_RGBA16Float: {
			uint16_t v[4] = { Math::make_half_float(p_color.r), Math::make_half_float(p_color.g),
				Math::make_half_float(p_color.b), Math::make_half_float(p_color.a) };
			memcpy(r_texel, v, 8);
		} break;

		// Uint16 formats.
		case WGPUTextureFormat_R16Uint: {
			uint16_t v = (uint16_t)p_color.r;
			memcpy(r_texel, &v, 2);
		} break;
		case WGPUTextureFormat_RG16Uint: {
			uint16_t v[2] = { (uint16_t)p_color.r, (uint16_t)p_color.g };
			memcpy(r_texel, v, 4);
		} break;
		case WGPUTextureFormat_RGBA16Uint: {
			uint16_t v[4] = { (uint16_t)p_color.r, (uint16_t)p_color.g,
				(uint16_t)p_color.b, (uint16_t)p_color.a };
			memcpy(r_texel, v, 8);
		} break;

		default:
			// Zero-fill for unhandled formats (already zeroed).
			break;
	}
}

void RenderingDeviceDriverWebGPU::command_clear_color_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, const Color &p_color, const TextureSubresourceRange &p_subresources) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(tex);

	cmd->end_active_encoder();

	if (tex->usage & WGPUTextureUsage_RenderAttachment) {
		// Fast path: clear via a zero-draw render pass (requires RenderAttachment usage).
		for (uint32_t mip = p_subresources.base_mipmap; mip < p_subresources.base_mipmap + p_subresources.mipmap_count; mip++) {
			for (uint32_t layer = p_subresources.base_layer; layer < p_subresources.base_layer + p_subresources.layer_count; layer++) {
				WGPUTextureViewDescriptor view_desc = {};
				view_desc.format = tex->format;
				view_desc.dimension = WGPUTextureViewDimension_2D;
				view_desc.baseMipLevel = mip;
				view_desc.mipLevelCount = 1;
				view_desc.baseArrayLayer = layer;
				view_desc.arrayLayerCount = 1;
				view_desc.aspect = WGPUTextureAspect_All;

				WGPUTextureView view = wgpuTextureCreateView(tex->view_source, &view_desc);

				WGPURenderPassColorAttachment color_att = {};
				color_att.view = view;
				color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
				color_att.loadOp = WGPULoadOp_Clear;
				color_att.storeOp = WGPUStoreOp_Store;
				color_att.clearValue = { p_color.r, p_color.g, p_color.b, p_color.a };

				WGPURenderPassDescriptor rp_desc = {};
				rp_desc.colorAttachmentCount = 1;
				rp_desc.colorAttachments = &color_att;

				WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &rp_desc);
				wgpuRenderPassEncoderEnd(pass);
				wgpuRenderPassEncoderRelease(pass);
				wgpuTextureViewRelease(view);
			}
		}
	} else {
		// Fallback for textures without RenderAttachment usage (e.g. storage-only
		// compute textures). Uses wgpuQueueWriteTexture to fill with the clear color.
		ERR_FAIL_COND_MSG(!(tex->usage & WGPUTextureUsage_CopyDst),
				"Cannot clear texture: texture has neither RenderAttachment nor CopyDst usage.");

		uint32_t bpp = _wgpu_format_byte_size(tex->format);
		ERR_FAIL_COND_MSG(bpp == 0,
				"Cannot clear texture via writeTexture: unsupported format.");

		bool is_zero = (p_color.r == 0.0f && p_color.g == 0.0f && p_color.b == 0.0f && p_color.a == 0.0f);

		// Encode one texel of the clear color.
		uint8_t texel[16] = {};
		if (!is_zero) {
			_encode_clear_texel(tex->format, p_color, texel);
		}

		for (uint32_t mip = p_subresources.base_mipmap; mip < p_subresources.base_mipmap + p_subresources.mipmap_count; mip++) {
			for (uint32_t layer = p_subresources.base_layer; layer < p_subresources.base_layer + p_subresources.layer_count; layer++) {
				uint32_t w = MAX(1u, tex->width >> mip);
				uint32_t h = MAX(1u, tex->height >> mip);
				uint32_t row_bytes = w * bpp;

				// Fill a CPU buffer with the clear texel pattern.
				Vector<uint8_t> data;
				data.resize(row_bytes * h);
				uint8_t *ptr = data.ptrw();

				if (is_zero) {
					memset(ptr, 0, data.size());
				} else {
					for (uint32_t y = 0; y < h; y++) {
						for (uint32_t x = 0; x < w; x++) {
							memcpy(ptr + y * row_bytes + x * bpp, texel, bpp);
						}
					}
				}

				WGPUTexelCopyTextureInfo dst = {};
				dst.texture = tex->gpu_handle();
				dst.mipLevel = mip;
				dst.origin = { 0, 0, layer };
				dst.aspect = WGPUTextureAspect_All;

				WGPUTexelCopyBufferLayout layout = {};
				layout.offset = 0;
				layout.bytesPerRow = row_bytes;
				layout.rowsPerImage = h;

				WGPUExtent3D extent = { w, h, 1 };

				wgpuQueueWriteTexture(queue, &dst, ptr, data.size(), &layout, &extent);
			}
		}
	}
}

void RenderingDeviceDriverWebGPU::command_clear_depth_stencil_texture(CommandBufferID p_cmd_buffer, TextureID p_texture, TextureLayout p_texture_layout, float p_depth, uint8_t p_stencil, const TextureSubresourceRange &p_subresources) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *tex = (WGTexture *)(p_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(tex);

	cmd->end_active_encoder();

	bool has_depth = is_depth_format_wgpu(tex->format);
	bool has_stencil = has_stencil_wgpu(tex->format);

	for (uint32_t mip = p_subresources.base_mipmap; mip < p_subresources.base_mipmap + p_subresources.mipmap_count; mip++) {
		for (uint32_t layer = p_subresources.base_layer; layer < p_subresources.base_layer + p_subresources.layer_count; layer++) {
			WGPUTextureViewDescriptor view_desc = {};
			view_desc.format = tex->format;
			view_desc.dimension = WGPUTextureViewDimension_2D;
			view_desc.baseMipLevel = mip;
			view_desc.mipLevelCount = 1;
			view_desc.baseArrayLayer = layer;
			view_desc.arrayLayerCount = 1;
			view_desc.aspect = WGPUTextureAspect_All;

			WGPUTextureView view = wgpuTextureCreateView(tex->view_source, &view_desc);

			WGPURenderPassDepthStencilAttachment ds_att = {};
			ds_att.view = view;
			if (has_depth) {
				ds_att.depthLoadOp = WGPULoadOp_Clear;
				ds_att.depthStoreOp = WGPUStoreOp_Store;
				ds_att.depthClearValue = p_depth;
			} else {
				ds_att.depthLoadOp = WGPULoadOp_Undefined;
				ds_att.depthStoreOp = WGPUStoreOp_Undefined;
				ds_att.depthReadOnly = true;
			}
			if (has_stencil) {
				ds_att.stencilLoadOp = WGPULoadOp_Clear;
				ds_att.stencilStoreOp = WGPUStoreOp_Store;
				ds_att.stencilClearValue = p_stencil;
			} else {
				ds_att.stencilLoadOp = WGPULoadOp_Undefined;
				ds_att.stencilStoreOp = WGPUStoreOp_Undefined;
				ds_att.stencilReadOnly = true;
			}

			WGPURenderPassDescriptor rp_desc = {};
			rp_desc.depthStencilAttachment = &ds_att;

			WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &rp_desc);
			wgpuRenderPassEncoderEnd(pass);
			wgpuRenderPassEncoderRelease(pass);
			wgpuTextureViewRelease(view);
		}
	}
}

void RenderingDeviceDriverWebGPU::command_copy_buffer_to_texture(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, VectorView<BufferTextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *src = (WGBuffer *)(p_src_buffer.id);
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	// Compressed formats must pass a block-aligned copy extent. For mip levels
	// smaller than the block size (e.g. a 2x2 mip of a BC3 texture), the physical
	// size is the next block multiple, and WebGPU rejects non-multiple extents.
	uint32_t block_w = 1, block_h = 1;
	if (dst->rd_format != DATA_FORMAT_MAX) {
		get_compressed_image_format_block_dimensions(dst->rd_format, block_w, block_h);
	}

	// When the source buffer has a CPU shadow_map (WebGPU staging buffers),
	// use wgpuQueueWriteTexture to upload data directly from CPU memory
	// to the GPU texture. This bypasses the GPU staging buffer entirely,
	// avoiding the need to flush shadow_map → GPU buffer first.
	const bool use_write_texture = (src->shadow_map != nullptr);

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferTextureCopyRegion &region = p_regions[i];

		uint32_t copy_w = (uint32_t)region.texture_region_size.x;
		uint32_t copy_h = (uint32_t)region.texture_region_size.y;
		if (block_w > 1 || block_h > 1) {
			copy_w = ((copy_w + block_w - 1) / block_w) * block_w;
			copy_h = ((copy_h + block_h - 1) / block_h) * block_h;
		}

		WGPUTexelCopyTextureInfo dst_copy = {};
		dst_copy.texture = dst->gpu_handle();
		dst_copy.mipLevel = region.texture_subresource.mipmap;
		dst_copy.origin = { (uint32_t)region.texture_offset.x, (uint32_t)region.texture_offset.y, region.texture_subresource.layer };
		dst_copy.aspect = WGPUTextureAspect_All;

		WGPUExtent3D extent = { copy_w, copy_h, (uint32_t)region.texture_region_size.z };

		uint32_t aligned_bpr = ((region.row_pitch + 255) / 256) * 256; // 256-byte aligned.
		uint32_t rows_per_image = (block_h > 1) ? (copy_h / block_h) : copy_h;

		if (use_write_texture) {
			// Direct CPU→GPU upload via queue.writeTexture — no GPU staging buffer needed.
			WGPUTexelCopyBufferLayout layout = {};
			layout.offset = 0; // Data pointer already points to the right offset.
			layout.bytesPerRow = aligned_bpr;
			layout.rowsPerImage = rows_per_image;

			const uint8_t *data_ptr = src->shadow_map + region.buffer_offset;
			uint64_t data_size = (uint64_t)aligned_bpr * rows_per_image;

			wgpuQueueWriteTexture(queue, &dst_copy, data_ptr, data_size, &layout, &extent);
		} else {
			// Standard path: copy from GPU buffer to texture.
			WGPUTexelCopyBufferInfo src_info = {};
			src_info.buffer = src->handle;
			src_info.layout.offset = region.buffer_offset;
			src_info.layout.bytesPerRow = aligned_bpr;
			src_info.layout.rowsPerImage = rows_per_image;

			wgpuCommandEncoderCopyBufferToTexture(cmd->encoder, &src_info, &dst_copy, &extent);
		}
	}

	// When we used the writeTexture path, data went directly from the shadow_map
	// to the GPU texture — the staging buffer's GPU-side copy was never involved.
	// Clear map_dirty so the subsequent buffer_unmap() is a no-op instead of
	// redundantly flushing the entire staging buffer (often 32MB) to the GPU.
	if (use_write_texture) {
		src->map_dirty = false;
	}

	// If the destination texture has read_write storage shadow copies,
	// replay the same writeTexture data to each shadow. This is more
	// reliable than texture-to-texture copy on Firefox/wgpu, which can
	// race with the preceding writeTexture on some implementations.
	WGPUTexture dst_handle = dst->gpu_handle();
	if (use_write_texture && rw_shadow_copy_map.has(dst_handle)) {
		const LocalVector<WGPUTexture> &shadows = rw_shadow_copy_map[dst_handle];
		static int _shadow_write_log = 0;
		if (_shadow_write_log < 10) {
			WEBGPU_DIAG({ console.log('[SHADOW-WRITE] shadow_count=' + $0 + ' regions=' + $1); }, (int)shadows.size(), (int)p_regions.size());
			_shadow_write_log++;
		}
		for (WGPUTexture shadow : shadows) {
			for (uint32_t i = 0; i < p_regions.size(); i++) {
				const BufferTextureCopyRegion &region = p_regions[i];

				uint32_t s_copy_w = (uint32_t)region.texture_region_size.x;
				uint32_t s_copy_h = (uint32_t)region.texture_region_size.y;
				if (block_w > 1 || block_h > 1) {
					s_copy_w = ((s_copy_w + block_w - 1) / block_w) * block_w;
					s_copy_h = ((s_copy_h + block_h - 1) / block_h) * block_h;
				}

				WGPUTexelCopyTextureInfo shd_copy = {};
				shd_copy.texture = shadow;
				shd_copy.mipLevel = region.texture_subresource.mipmap;
				shd_copy.origin = { (uint32_t)region.texture_offset.x, (uint32_t)region.texture_offset.y, region.texture_subresource.layer };
				shd_copy.aspect = WGPUTextureAspect_All;

				uint32_t s_aligned_bpr = ((region.row_pitch + 255) / 256) * 256;
				uint32_t s_rows = (block_h > 1) ? (s_copy_h / block_h) : s_copy_h;

				WGPUTexelCopyBufferLayout shd_layout = {};
				shd_layout.offset = 0;
				shd_layout.bytesPerRow = s_aligned_bpr;
				shd_layout.rowsPerImage = s_rows;

				const uint8_t *data_ptr = src->shadow_map + region.buffer_offset;
				uint64_t data_size = (uint64_t)s_aligned_bpr * s_rows;

				WGPUExtent3D shd_extent = { s_copy_w, s_copy_h, (uint32_t)region.texture_region_size.z };
				wgpuQueueWriteTexture(queue, &shd_copy, data_ptr, data_size, &shd_layout, &shd_extent);
			}
		}
	} else if (!use_write_texture && rw_shadow_copy_map.has(dst_handle)) {
		// GPU buffer path: record shadow copies on the same command encoder so
		// they execute after the preceding buffer-to-texture copy.
		const LocalVector<WGPUTexture> &shadows = rw_shadow_copy_map[dst_handle];
		for (WGPUTexture shadow : shadows) {
			WGPUTexelCopyTextureInfo shd_src = {};
			shd_src.texture = dst_handle;
			shd_src.mipLevel = 0;
			shd_src.origin = { 0, 0, 0 };
			shd_src.aspect = WGPUTextureAspect_All;
			WGPUTexelCopyTextureInfo shd_dst = {};
			shd_dst.texture = shadow;
			shd_dst.mipLevel = 0;
			shd_dst.origin = { 0, 0, 0 };
			shd_dst.aspect = WGPUTextureAspect_All;
			// dst->depth is always 1 for a 2D(-array) texture -- use layers
			// there instead, same dimension-aware selection as
			// _create_rw_shadow_bind_entry(). Task 9.5 Round 46.
			uint32_t shd_copy_depth = (dst->dimension == WGPUTextureDimension_3D) ? dst->depth : dst->layers;
			WGPUExtent3D shd_extent = { dst->width, dst->height, shd_copy_depth };
			wgpuCommandEncoderCopyTextureToTexture(cmd->encoder, &shd_src, &shd_dst, &shd_extent);
		}
	}
}

// Single-call multi-layer buffer→texture upload. The WGSL queue.writeTexture
// API supports `extent.depthOrArrayLayers > 1` for 2D-array textures, with
// consecutive layers laid out at `rowsPerImage * bytesPerRow` byte stride
// in the source buffer. The engine packs all N layers contiguously in the
// staging buffer, so we issue a single wgpuQueueWriteTexture covering
// [layer .. layer+N) instead of looping. Each wgpuQueueWriteTexture call
// crosses wasm→JS→WebGPU and incurs ~9-11 ms fixed overhead, so this saves
// (N-1) × ~10 ms during initial Texture2DArray uploads.
void RenderingDeviceDriverWebGPU::command_copy_buffer_to_texture_layered(CommandBufferID p_cmd_buffer, BufferID p_src_buffer, TextureID p_dst_texture, TextureLayout p_dst_texture_layout, const BufferTextureCopyRegion &p_base_region, uint32_t p_layer_count, uint64_t p_per_layer_byte_stride) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *src = (WGBuffer *)(p_src_buffer.id);
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	if (p_layer_count == 0) {
		return;
	}

	cmd->end_active_encoder();

	// Block-aligned copy extent (matches command_copy_buffer_to_texture).
	uint32_t block_w = 1, block_h = 1;
	if (dst->rd_format != DATA_FORMAT_MAX) {
		get_compressed_image_format_block_dimensions(dst->rd_format, block_w, block_h);
	}

	uint32_t copy_w = (uint32_t)p_base_region.texture_region_size.x;
	uint32_t copy_h = (uint32_t)p_base_region.texture_region_size.y;
	if (block_w > 1 || block_h > 1) {
		copy_w = ((copy_w + block_w - 1) / block_w) * block_w;
		copy_h = ((copy_h + block_h - 1) / block_h) * block_h;
	}

	WGPUTexelCopyTextureInfo dst_copy = {};
	dst_copy.texture = dst->gpu_handle();
	dst_copy.mipLevel = p_base_region.texture_subresource.mipmap;
	// .origin.z is the starting array layer; extent.depthOrArrayLayers covers N.
	dst_copy.origin = { (uint32_t)p_base_region.texture_offset.x, (uint32_t)p_base_region.texture_offset.y, p_base_region.texture_subresource.layer };
	dst_copy.aspect = WGPUTextureAspect_All;

	WGPUExtent3D extent = { copy_w, copy_h, p_layer_count };

	uint32_t aligned_bpr = ((p_base_region.row_pitch + 255) / 256) * 256; // 256-byte aligned per WebGPU spec.
	uint32_t rows_per_image = (block_h > 1) ? (copy_h / block_h) : copy_h;

	// Sanity check: layer stride must match aligned_bpr * rows_per_image, else
	// consecutive layers in staging won't be at the offsets WebGPU expects.
	// _texture_initialize_layered packs with this exact stride.
	const uint64_t expected_stride = (uint64_t)aligned_bpr * rows_per_image;
	if (p_per_layer_byte_stride != expected_stride) {
		// Fall back to the per-layer default behavior — preserves correctness
		// at the cost of the optimization. Should not happen with our caller.
		for (uint32_t i = 0; i < p_layer_count; i++) {
			BufferTextureCopyRegion r = p_base_region;
			r.texture_subresource.layer += i;
			r.buffer_offset += i * p_per_layer_byte_stride;
			command_copy_buffer_to_texture(p_cmd_buffer, p_src_buffer, p_dst_texture, p_dst_texture_layout, r);
		}
		return;
	}

	const bool use_write_texture = (src->shadow_map != nullptr);

	if (use_write_texture) {
		// Direct CPU→GPU upload via queue.writeTexture covering all N layers.
		WGPUTexelCopyBufferLayout layout = {};
		layout.offset = 0;
		layout.bytesPerRow = aligned_bpr;
		layout.rowsPerImage = rows_per_image;

		const uint8_t *data_ptr = src->shadow_map + p_base_region.buffer_offset;
		uint64_t data_size = expected_stride * (uint64_t)p_layer_count;

		wgpuQueueWriteTexture(queue, &dst_copy, data_ptr, data_size, &layout, &extent);

		// Data went directly from shadow_map → GPU texture. Clear map_dirty so
		// buffer_unmap() doesn't redundantly flush the entire staging buffer.
		src->map_dirty = false;
	} else {
		// Standard path: GPU buffer → texture, single command covering N layers.
		WGPUTexelCopyBufferInfo src_info = {};
		src_info.buffer = src->handle;
		src_info.layout.offset = p_base_region.buffer_offset;
		src_info.layout.bytesPerRow = aligned_bpr;
		src_info.layout.rowsPerImage = rows_per_image;

		wgpuCommandEncoderCopyBufferToTexture(cmd->encoder, &src_info, &dst_copy, &extent);
	}
}

// Direct CPU->GPU multi-layer texture write. Mirrors the writeTexture branch
// of command_copy_buffer_to_texture_layered, but takes a CPU pointer directly
// instead of going through a transfer worker's GPU staging buffer + shadow_map.
//
// Save vs transfer-worker path on tier 1024 (75 layers, 300 MB total):
//   - 1x wgpuDeviceCreateBuffer(300 MB) eliminated (peak VRAM -300 MB)
//   - 1x command encoder begin/end eliminated
//   - 1x pipeline-barrier no-op eliminated
//   - The single multi-layer wgpuQueueWriteTexture is the same as before.
void RenderingDeviceDriverWebGPU::texture_initialize_direct_layered(TextureID p_dst_texture, TextureLayout p_dst_layout, const uint8_t *p_cpu_data, uint64_t p_total_size, uint32_t p_aligned_bpr, uint32_t p_rows_per_image, uint32_t p_width, uint32_t p_height, uint32_t p_layer_count, uint32_t p_base_layer, uint32_t p_mip_level) {
	WGTexture *dst = (WGTexture *)(p_dst_texture.id);
	ERR_FAIL_NULL(dst);
	ERR_FAIL_NULL(p_cpu_data);
	if (p_layer_count == 0 || p_total_size == 0) {
		return;
	}

	// Block-aligned copy extent (matches command_copy_buffer_to_texture).
	uint32_t block_w = 1, block_h = 1;
	if (dst->rd_format != DATA_FORMAT_MAX) {
		get_compressed_image_format_block_dimensions(dst->rd_format, block_w, block_h);
	}
	uint32_t copy_w = p_width;
	uint32_t copy_h = p_height;
	if (block_w > 1 || block_h > 1) {
		copy_w = ((copy_w + block_w - 1) / block_w) * block_w;
		copy_h = ((copy_h + block_h - 1) / block_h) * block_h;
	}

	WGPUTexelCopyTextureInfo dst_copy = {};
	dst_copy.texture = dst->gpu_handle();
	dst_copy.mipLevel = p_mip_level;
	dst_copy.origin = { 0, 0, p_base_layer };
	dst_copy.aspect = WGPUTextureAspect_All;

	WGPUExtent3D extent = { copy_w, copy_h, p_layer_count };

	WGPUTexelCopyBufferLayout layout = {};
	layout.offset = 0;
	layout.bytesPerRow = p_aligned_bpr;
	layout.rowsPerImage = p_rows_per_image;

	wgpuQueueWriteTexture(queue, &dst_copy, p_cpu_data, p_total_size, &layout, &extent);
}

void RenderingDeviceDriverWebGPU::command_copy_texture_to_buffer(CommandBufferID p_cmd_buffer, TextureID p_src_texture, TextureLayout p_src_texture_layout, BufferID p_dst_buffer, VectorView<BufferTextureCopyRegion> p_regions) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGTexture *src = (WGTexture *)(p_src_texture.id);
	WGBuffer *dst = (WGBuffer *)(p_dst_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(src);
	ERR_FAIL_NULL(dst);

	cmd->end_active_encoder();

	// Block-alignment: see command_copy_buffer_to_texture for the rationale.
	uint32_t block_w = 1, block_h = 1;
	if (src->rd_format != DATA_FORMAT_MAX) {
		get_compressed_image_format_block_dimensions(src->rd_format, block_w, block_h);
	}

	for (uint32_t i = 0; i < p_regions.size(); i++) {
		const BufferTextureCopyRegion &region = p_regions[i];

		uint32_t copy_w = (uint32_t)region.texture_region_size.x;
		uint32_t copy_h = (uint32_t)region.texture_region_size.y;
		if (block_w > 1 || block_h > 1) {
			copy_w = ((copy_w + block_w - 1) / block_w) * block_w;
			copy_h = ((copy_h + block_h - 1) / block_h) * block_h;
		}

		WGPUTexelCopyTextureInfo src_copy = {};
		src_copy.texture = src->gpu_handle();
		src_copy.mipLevel = region.texture_subresource.mipmap;
		src_copy.origin = { (uint32_t)region.texture_offset.x, (uint32_t)region.texture_offset.y, region.texture_subresource.layer };
		src_copy.aspect = WGPUTextureAspect_All;

		// WGPUTexelCopyBufferInfo combines the buffer handle + layout (Dawn API)
		// Use the actual GPU texture's pixel size for bytesPerRow — promoted
		// formats (R8→R32Float etc.) have a different bpp than the engine expects.
		uint32_t gpu_bpp = wgpu_format_pixel_size(src->format);
		uint32_t actual_row_bytes = copy_w * gpu_bpp;
		WGPUTexelCopyBufferInfo dst_info = {};
		dst_info.buffer = dst->handle;
		dst_info.layout.offset = region.buffer_offset;
		dst_info.layout.bytesPerRow = ((actual_row_bytes + 255) / 256) * 256;
		dst_info.layout.rowsPerImage = (block_h > 1) ? (copy_h / block_h) : copy_h;

		WGPUExtent3D extent = { copy_w, copy_h, (uint32_t)region.texture_region_size.z };

		wgpuCommandEncoderCopyTextureToBuffer(cmd->encoder, &src_copy, &dst_info, &extent);
	}
}

// =============================================================================
// PIPELINE
// =============================================================================

void RenderingDeviceDriverWebGPU::pipeline_free(PipelineID p_pipeline) {
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(pw);
	if (pw->type == WGPipelineWrapper::RENDER && pw->render_handle) {
		wgpuRenderPipelineRelease(pw->render_handle);
		if (pw->render_handle_u16) {
			wgpuRenderPipelineRelease(pw->render_handle_u16);
		}
	} else if (pw->type == WGPipelineWrapper::COMPUTE && pw->compute_handle) {
		wgpuComputePipelineRelease(pw->compute_handle);
	}
	// Release any specialized shader modules owned by this pipeline.
	for (int i = 0; i < 6; i++) {
		if (pw->specialized_modules[i]) {
			wgpuShaderModuleRelease(pw->specialized_modules[i]);
		}
	}
	delete pw;
}

void RenderingDeviceDriverWebGPU::command_bind_push_constants(CommandBufferID p_cmd_buffer, ShaderID p_shader, uint32_t p_first_index, VectorView<uint32_t> p_data) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	uint32_t offset = p_first_index * sizeof(uint32_t);
	uint32_t size = p_data.size() * sizeof(uint32_t);
	ERR_FAIL_COND(offset + size > WGCommandBuffer::MAX_PUSH_CONSTANT_SIZE);

	memcpy(cmd->push_constant_data + offset, &p_data[0], size);
	cmd->push_constant_data_len = MAX(cmd->push_constant_data_len, offset + size);
	cmd->push_constants_dirty = true;
}

void RenderingDeviceDriverWebGPU::_flush_push_constants(WGCommandBuffer *p_cmd_buf, WGShader *p_shader) {
	if (!p_cmd_buf->push_constants_dirty || p_cmd_buf->push_constant_data_len == 0 || !p_shader) {
		perf.push_constant_skipped++;
		return;
	}
	if (p_shader->push_constant_bind_group == UINT32_MAX || !push_constant_bind_group) {
		p_cmd_buf->push_constants_dirty = false;
		perf.push_constant_skipped++;
		return; // Shader has no push constants.
	}

	perf.push_constant_writes++;

	// Write push constant data to CPU shadow buffer (batched GPU write at submit time).
	uint32_t aligned_size = (p_cmd_buf->push_constant_data_len + PUSH_CONSTANT_SLOT_ALIGNMENT - 1) & ~(PUSH_CONSTANT_SLOT_ALIGNMENT - 1);

	if (push_constant_ring_offset + aligned_size > PUSH_CONSTANT_RING_SIZE) {
		// Ring overflow: flush + submit to freeze all prior dispatches/draws' data, then reset.
		// This guarantees earlier work in the submitted command buffer sees correct push
		// constant values (wgpuQueueWriteBuffer is ordered-before submit).
		perf.ring_overflows++;

		// Remember which encoder type was active so we can restart the correct one.
		const bool was_render = (p_cmd_buf->render_encoder != nullptr);
		const bool was_compute = (p_cmd_buf->compute_encoder != nullptr);

		// 1. Flush any dirty shadow data.
		if (push_constant_shadow_dirty_start < push_constant_shadow_dirty_end) {
			wgpuQueueWriteBuffer(queue, push_constant_ring_buffer, push_constant_shadow_dirty_start,
					push_constant_shadow + push_constant_shadow_dirty_start,
					push_constant_shadow_dirty_end - push_constant_shadow_dirty_start);
			push_constant_shadow_dirty_start = UINT32_MAX;
			push_constant_shadow_dirty_end = 0;
		}

		// 2. End current pass (render or compute) before finishing encoder.
		if (p_cmd_buf->render_encoder) {
			wgpuRenderPassEncoderEnd(p_cmd_buf->render_encoder);
			wgpuRenderPassEncoderRelease(p_cmd_buf->render_encoder);
			p_cmd_buf->render_encoder = nullptr;
		}
		if (p_cmd_buf->compute_encoder) {
			wgpuComputePassEncoderEnd(p_cmd_buf->compute_encoder);
			wgpuComputePassEncoderRelease(p_cmd_buf->compute_encoder);
			p_cmd_buf->compute_encoder = nullptr;
		}

		// 3. Finish + submit the current command buffer (non-blocking).
		if (p_cmd_buf->encoder) {
			WGPUCommandBuffer finished = wgpuCommandEncoderFinish(p_cmd_buf->encoder, nullptr);
			if (finished) {
				wgpuQueueSubmit(queue, 1, &finished);
				wgpuCommandBufferRelease(finished);
			}
			wgpuCommandEncoderRelease(p_cmd_buf->encoder);
			p_cmd_buf->encoder = nullptr;
		}

		// 4. Reset ring offset — submitted data is frozen by queue ordering.
		push_constant_ring_offset = 0;

		// 5. Create fresh encoder.
		p_cmd_buf->encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

		if (was_compute) {
			// 6a. Restart compute pass and rebind state.
			WGPUComputePassDescriptor cp_desc = {};
			p_cmd_buf->compute_encoder = wgpuCommandEncoderBeginComputePass(p_cmd_buf->encoder, &cp_desc);
			p_cmd_buf->active_encoder = WGCommandBuffer::COMPUTE;

			// Rebind compute pipeline.
			WGPipelineWrapper *pw = p_cmd_buf->render_state.current_pipeline;
			if (pw) {
				wgpuComputePassEncoderSetPipeline(p_cmd_buf->compute_encoder, pw->compute_handle);
				if (pw->shader) {
					for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
						wgpuComputePassEncoderSetBindGroup(p_cmd_buf->compute_encoder, gap_idx, empty_bind_group, 0, nullptr);
					}
				}
			}

			// Rebind all bind groups from saved state.
			for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
				const auto &bs = p_cmd_buf->last_bound_state[i];
				if (bs.group) {
					wgpuComputePassEncoderSetBindGroup(p_cmd_buf->compute_encoder, i, bs.group, bs.dynamic_offset_count, bs.dynamic_offsets);
				}
			}

			// Invalidate redundancy caches.
			for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
				p_cmd_buf->bound_bind_groups[i] = nullptr;
			}
		} else if (was_render) {
			// 6b. Restart render pass with LoadOp::Load to preserve framebuffer content.
			WGRenderPass *rp = p_cmd_buf->render_state.render_pass;
			WGFramebuffer *fb = p_cmd_buf->render_state.framebuffer;
			if (rp && fb) {
				const WGRenderPass::SubpassInfo &subpass = rp->subpasses[p_cmd_buf->render_state.current_subpass];

				LocalVector<WGPURenderPassColorAttachment> color_attachments;
				for (uint32_t i = 0; i < subpass.color_references.size(); i++) {
					const RDD::AttachmentReference &ref = subpass.color_references[i];
					WGPURenderPassColorAttachment att = {};
					att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

					if (ref.attachment == RDD::AttachmentReference::UNUSED) {
						att.view = nullptr;
						att.loadOp = WGPULoadOp_Load;
						att.storeOp = WGPUStoreOp_Discard;
						color_attachments.push_back(att);
						continue;
					}

					if (ref.attachment < fb->attachment_views.size()) {
						att.view = fb->attachment_views[ref.attachment];
					}
					if (i < subpass.resolve_references.size()) {
						const RDD::AttachmentReference &res_ref = subpass.resolve_references[i];
						if (res_ref.attachment != RDD::AttachmentReference::UNUSED &&
								res_ref.attachment < fb->attachment_views.size()) {
							att.resolveTarget = fb->attachment_views[res_ref.attachment];
						}
					}
					att.loadOp = WGPULoadOp_Load;
					att.storeOp = WGPUStoreOp_Store;
					color_attachments.push_back(att);
				}

				WGPURenderPassDepthStencilAttachment ds_att = {};
				WGPURenderPassDepthStencilAttachment *ds_att_ptr = nullptr;
				const RDD::AttachmentReference &ds_ref = subpass.depth_stencil_reference;
				if (ds_ref.attachment != RDD::AttachmentReference::UNUSED &&
						ds_ref.attachment < fb->attachment_views.size()) {
					ds_att.view = fb->attachment_views[ds_ref.attachment];
					if (ds_ref.attachment < rp->attachments.size()) {
						WGPUTextureFormat wgpu_fmt = _data_format_to_wgpu(rp->attachments[ds_ref.attachment].format);
						bool has_depth = is_depth_format_wgpu(wgpu_fmt);
						bool has_stencil = has_stencil_wgpu(wgpu_fmt);
						if (has_depth) {
							ds_att.depthLoadOp = WGPULoadOp_Load;
							ds_att.depthStoreOp = WGPUStoreOp_Store;
						} else {
							ds_att.depthLoadOp = WGPULoadOp_Undefined;
							ds_att.depthStoreOp = WGPUStoreOp_Undefined;
							ds_att.depthReadOnly = true;
						}
						if (has_stencil) {
							ds_att.stencilLoadOp = WGPULoadOp_Load;
							ds_att.stencilStoreOp = WGPUStoreOp_Store;
						} else {
							ds_att.stencilLoadOp = WGPULoadOp_Undefined;
							ds_att.stencilStoreOp = WGPUStoreOp_Undefined;
							ds_att.stencilReadOnly = true;
						}
					} else {
						ds_att.depthLoadOp = WGPULoadOp_Load;
						ds_att.depthStoreOp = WGPUStoreOp_Store;
						ds_att.depthClearValue = 1.0f;
						ds_att.stencilLoadOp = WGPULoadOp_Load;
						ds_att.stencilStoreOp = WGPUStoreOp_Store;
					}
					ds_att_ptr = &ds_att;
				}

				WGPURenderPassDescriptor pass_desc = {};
				pass_desc.colorAttachmentCount = color_attachments.size();
				pass_desc.colorAttachments = color_attachments.ptr();
				pass_desc.depthStencilAttachment = ds_att_ptr;

				p_cmd_buf->render_encoder = wgpuCommandEncoderBeginRenderPass(p_cmd_buf->encoder, &pass_desc);
				p_cmd_buf->active_encoder = WGCommandBuffer::RENDER;

				// Rebind render pipeline.
				WGPipelineWrapper *pw = p_cmd_buf->render_state.current_pipeline;
				if (pw) {
					wgpuRenderPassEncoderSetPipeline(p_cmd_buf->render_encoder, pw->render_handle);
					wgpuRenderPassEncoderSetStencilReference(p_cmd_buf->render_encoder, pw->stencil_reference);
					if (pw->shader) {
						for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
							wgpuRenderPassEncoderSetBindGroup(p_cmd_buf->render_encoder, gap_idx, empty_bind_group, 0, nullptr);
						}
					}
				}

				// Rebind all bind groups from saved state.
				for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
					const auto &bs = p_cmd_buf->last_bound_state[i];
					if (bs.group) {
						wgpuRenderPassEncoderSetBindGroup(p_cmd_buf->render_encoder, i, bs.group, bs.dynamic_offset_count, bs.dynamic_offsets);
					}
				}

				// Rebind vertex buffers.
				for (uint32_t i = 0; i < WGCommandBuffer::RenderState::MAX_VERTEX_BINDINGS; i++) {
					if (p_cmd_buf->render_state.current_vertex_buffers[i]) {
						wgpuRenderPassEncoderSetVertexBuffer(p_cmd_buf->render_encoder, i,
								p_cmd_buf->render_state.current_vertex_buffers[i],
								p_cmd_buf->render_state.current_vertex_offsets[i], WGPU_WHOLE_SIZE);
					}
				}

				// Rebind index buffer.
				if (p_cmd_buf->render_state.current_index_buffer) {
					wgpuRenderPassEncoderSetIndexBuffer(p_cmd_buf->render_encoder,
							p_cmd_buf->render_state.current_index_buffer,
							p_cmd_buf->render_state.current_index_format,
							p_cmd_buf->render_state.current_index_offset, WGPU_WHOLE_SIZE);
				}

				// Invalidate redundancy caches (state was just rebound fresh).
				for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
					p_cmd_buf->bound_bind_groups[i] = nullptr;
				}
			}
		}
	}

	memcpy(push_constant_shadow + push_constant_ring_offset, p_cmd_buf->push_constant_data, p_cmd_buf->push_constant_data_len);

	// Track dirty range for batched flush.
	if (push_constant_ring_offset < push_constant_shadow_dirty_start) {
		push_constant_shadow_dirty_start = push_constant_ring_offset;
	}
	uint32_t end = push_constant_ring_offset + aligned_size;
	if (end > push_constant_shadow_dirty_end) {
		push_constant_shadow_dirty_end = end;
	}

	uint32_t dynamic_offset = push_constant_ring_offset;

	// Choose which bind group to rebind at the PC group:
	// - If the shader has a merged layout (material + PC) AND the material uniform set
	//   was previously bound, use that combined bind group.
	// - Otherwise, fall back to the universal PC-only bind group.
	WGPUBindGroup pc_bind_group_to_use;
	bool use_merged = (p_shader->merged_pc_group_layout && p_cmd_buf->current_pc_bind_group != nullptr);
	if (use_merged) {
		pc_bind_group_to_use = p_cmd_buf->current_pc_bind_group;
	} else {
		pc_bind_group_to_use = push_constant_bind_group;
	}

	// Task 7.5: If the merged PC group also contains material dynamic-offset UBOs,
	// we need to preserve those offsets while patching in the new PC ring offset.
	// command_bind_*_uniform_sets saved the material offsets in
	// pc_group_material_dyn_offsets[] in binding order; the PC ring's own dynamic
	// offset is appended last (the PC ring binding is always the highest binding
	// in the merged group — see rendering_shader_container_webgpu.cpp).
	uint32_t dyn_offsets[WGCommandBuffer::MAX_PC_GROUP_MATERIAL_DYN + 1];
	uint32_t num_dyn_offsets;
	if (use_merged && p_cmd_buf->pc_group_material_dyn_count > 0) {
		uint32_t mat_count = p_cmd_buf->pc_group_material_dyn_count;
		if (mat_count > WGCommandBuffer::MAX_PC_GROUP_MATERIAL_DYN) {
			mat_count = WGCommandBuffer::MAX_PC_GROUP_MATERIAL_DYN;
		}
		for (uint32_t j = 0; j < mat_count; j++) {
			dyn_offsets[j] = p_cmd_buf->pc_group_material_dyn_offsets[j];
		}
		dyn_offsets[mat_count] = dynamic_offset;
		num_dyn_offsets = mat_count + 1;
	} else {
		dyn_offsets[0] = dynamic_offset;
		num_dyn_offsets = 1;
	}

	// Bind the push constant ring buffer bind group with the assembled dynamic offsets.
	if (p_cmd_buf->render_encoder) {
		wgpuRenderPassEncoderSetBindGroup(p_cmd_buf->render_encoder, p_shader->push_constant_bind_group, pc_bind_group_to_use, num_dyn_offsets, dyn_offsets);
	} else if (p_cmd_buf->compute_encoder) {
		wgpuComputePassEncoderSetBindGroup(p_cmd_buf->compute_encoder, p_shader->push_constant_bind_group, pc_bind_group_to_use, num_dyn_offsets, dyn_offsets);
	}

	p_cmd_buf->last_flushed_pc_offset = push_constant_ring_offset;
	push_constant_ring_offset += aligned_size;
	p_cmd_buf->push_constants_dirty = false;
}

bool RenderingDeviceDriverWebGPU::pipeline_cache_create(const Vector<uint8_t> &p_data) {
	return true; // No-op.
}

void RenderingDeviceDriverWebGPU::pipeline_cache_free() {
	// No-op.
}

size_t RenderingDeviceDriverWebGPU::pipeline_cache_query_size() {
	// We don't implement a pipeline cache, but must return non-zero so
	// RenderingDevice::update_pipeline_cache() doesn't emit an error every frame.
	return 1;
}

Vector<uint8_t> RenderingDeviceDriverWebGPU::pipeline_cache_serialize() {
	return Vector<uint8_t>();
}

// =============================================================================
// RENDERING
// =============================================================================

RDD::RenderPassID RenderingDeviceDriverWebGPU::render_pass_create(VectorView<Attachment> p_attachments, VectorView<Subpass> p_subpasses, VectorView<SubpassDependency> p_subpass_dependencies, uint32_t p_view_count, AttachmentReference p_fragment_density_map_attachment) {
	WGRenderPass *rp = new WGRenderPass();
	rp->view_count = p_view_count;

	for (uint32_t i = 0; i < p_attachments.size(); i++) {
		rp->attachments.push_back(p_attachments[i]);
	}

	for (uint32_t i = 0; i < p_subpasses.size(); i++) {
		WGRenderPass::SubpassInfo sp;
		sp.color_references = p_subpasses[i].color_references;
		sp.input_references = p_subpasses[i].input_references;
		sp.depth_stencil_reference = p_subpasses[i].depth_stencil_reference;
		sp.resolve_references = p_subpasses[i].resolve_references;
		rp->subpasses.push_back(sp);
	}

	return RenderPassID(rp);
}

void RenderingDeviceDriverWebGPU::render_pass_free(RenderPassID p_render_pass) {
	WGRenderPass *rp = (WGRenderPass *)(p_render_pass.id);
	delete rp;
}

void RenderingDeviceDriverWebGPU::command_begin_render_pass(CommandBufferID p_cmd_buffer, RenderPassID p_render_pass, FramebufferID p_framebuffer, CommandBufferType p_cmd_buffer_type, const Rect2i &p_rect, VectorView<RenderPassClearValue> p_clear_values) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGRenderPass *rp = (WGRenderPass *)(p_render_pass.id);
	WGFramebuffer *fb = (WGFramebuffer *)(p_framebuffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(rp);
	ERR_FAIL_NULL(fb);

	perf.render_passes++;

	// End any active encoder.
	cmd->end_active_encoder();

	// Invalidate bind group state tracking (new encoder = clean state).
	cmd->invalidate_bind_groups();

	// Store render state.
	cmd->render_state.render_pass = rp;
	cmd->render_state.framebuffer = fb;
	cmd->render_state.current_subpass = 0;
	cmd->render_state.render_area_width = p_rect.size.x > 0 ? (uint32_t)p_rect.size.x : fb->width;
	cmd->render_state.render_area_height = p_rect.size.y > 0 ? (uint32_t)p_rect.size.y : fb->height;

	ERR_FAIL_COND(rp->subpasses.size() == 0);
	const WGRenderPass::SubpassInfo &subpass = rp->subpasses[0];

	// Track ONLY subpass-referenced attachments for intra-pass conflict detection.
	// Only textures in the actual render pass descriptor create sync scope constraints.
	cmd->render_state.reset_current_pass_attachments();
	for (uint32_t i = 0; i < subpass.color_references.size(); i++) {
		uint32_t att_idx = subpass.color_references[i].attachment;
		if (att_idx != RDD::AttachmentReference::UNUSED && att_idx < fb->attachments.size()) {
			WGTexture *tex = fb->attachments[att_idx];
			if (tex) {
				cmd->render_state.add_current_pass_attachment(tex->gpu_handle());
			}
		}
	}
	if (subpass.depth_stencil_reference.attachment != RDD::AttachmentReference::UNUSED &&
			subpass.depth_stencil_reference.attachment < fb->attachments.size()) {
		WGTexture *tex = fb->attachments[subpass.depth_stencil_reference.attachment];
		if (tex) {
			cmd->render_state.add_current_pass_attachment(tex->gpu_handle());
		}
	}

	// Proactive encoder isolation: if any current-pass attachment has BOTH
	// TextureBinding and RenderAttachment usage, this pass may cause an
	// intra-pass sync scope conflict. Split the encoder BEFORE the pass so
	// previous rendering work is preserved even if this pass's command
	// buffer gets invalidated by the conflict.
	{
		bool has_dual_usage = false;
		for (uint32_t i = 0; i < cmd->render_state.current_pass_attachment_count; i++) {
			// We don't have the WGTexture* for current_pass_attachments (only WGPUTexture),
			// so check the framebuffer's attachments for usage bits.
			for (uint32_t j = 0; j < fb->attachments.size(); j++) {
				WGTexture *tex = fb->attachments[j];
				if (tex) {
					WGPUTexture gpu_tex = tex->gpu_handle();
					if (gpu_tex == cmd->render_state.current_pass_attachments[i] &&
							(tex->usage & WGPUTextureUsage_TextureBinding) &&
							(tex->usage & WGPUTextureUsage_RenderAttachment)) {
						has_dual_usage = true;
						break;
					}
				}
			}
			if (has_dual_usage) {
				break;
			}
		}
		if (has_dual_usage && cmd->encoder) {
			// Flush push constant ring buffer before mid-frame submit.
			if (push_constant_shadow_dirty_start < push_constant_shadow_dirty_end) {
				wgpuQueueWriteBuffer(queue, push_constant_ring_buffer, push_constant_shadow_dirty_start,
						push_constant_shadow + push_constant_shadow_dirty_start,
						push_constant_shadow_dirty_end - push_constant_shadow_dirty_start);
				push_constant_shadow_dirty_start = UINT32_MAX;
				push_constant_shadow_dirty_end = 0;
			}
			// Split: submit everything so far, start a fresh encoder.
			WGPUCommandBuffer finished = wgpuCommandEncoderFinish(cmd->encoder, nullptr);
			if (finished) {
				wgpuQueueSubmit(queue, 1, &finished);
				wgpuCommandBufferRelease(finished);
			}
			wgpuCommandEncoderRelease(cmd->encoder);
			cmd->encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
			cmd->invalidate_bind_groups();
		}
	}

	// --- Helper lambdas for op mapping ---
	auto map_load_op = [](AttachmentLoadOp op) -> WGPULoadOp {
		switch (op) {
			case ATTACHMENT_LOAD_OP_LOAD:
				return WGPULoadOp_Load;
			case ATTACHMENT_LOAD_OP_CLEAR:
				return WGPULoadOp_Clear;
			default:
				return WGPULoadOp_Clear; // DONT_CARE → Clear (WebGPU has no DONT_CARE; Clear is safest)
		}
	};
	auto map_store_op = [](AttachmentStoreOp op) -> WGPUStoreOp {
		switch (op) {
			case ATTACHMENT_STORE_OP_STORE:
				return WGPUStoreOp_Store;
			default:
				return WGPUStoreOp_Discard; // DONT_CARE
		}
	};

	// --- Build color attachments ---
	// clear_values is indexed by attachment index, matching Vulkan/Godot convention.
	LocalVector<WGPURenderPassColorAttachment> color_attachments;
	for (uint32_t i = 0; i < subpass.color_references.size(); i++) {
		const RDD::AttachmentReference &ref = subpass.color_references[i];

		WGPURenderPassColorAttachment att = {};
		att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

		if (ref.attachment == RDD::AttachmentReference::UNUSED) {
			// Unused attachment slot: provide a dummy null entry.
			att.view = nullptr;
			att.loadOp = WGPULoadOp_Load;
			att.storeOp = WGPUStoreOp_Discard;
			color_attachments.push_back(att);
			continue;
		}

		// Attachment view from framebuffer.
		if (ref.attachment < fb->attachment_views.size()) {
			att.view = fb->attachment_views[ref.attachment];
		}

		// Resolve target (MSAA).
		if (i < subpass.resolve_references.size()) {
			const RDD::AttachmentReference &res_ref = subpass.resolve_references[i];
			if (res_ref.attachment != RDD::AttachmentReference::UNUSED &&
					res_ref.attachment < fb->attachment_views.size()) {
				att.resolveTarget = fb->attachment_views[res_ref.attachment];
			}
		}

		// Load/store ops and clear value from attachment description.
		if (ref.attachment < rp->attachments.size()) {
			const RDD::Attachment &attach_desc = rp->attachments[ref.attachment];
			att.loadOp = map_load_op(attach_desc.load_op);
			// WebGPU emulates Vulkan subpasses as separate render passes.
			// For the first subpass in a multi-subpass pass, force Store to
			// preserve MSAA data for subsequent subpasses (e.g. transparent pass).
			if (rp->subpasses.size() > 1) {
				att.storeOp = WGPUStoreOp_Store;
			} else {
				att.storeOp = map_store_op(attach_desc.store_op);
			}

			if (att.loadOp == WGPULoadOp_Clear && ref.attachment < p_clear_values.size()) {
				const Color &c = p_clear_values[ref.attachment].color;
				att.clearValue = { c.r, c.g, c.b, c.a };
			}

			// WebGPU swap chain textures have undefined content each frame.
			// Force LOAD→CLEAR so the alpha channel starts at 1.0 (opaque).
			if (rp->is_swap_chain_pass && att.loadOp == WGPULoadOp_Load) {
				att.loadOp = WGPULoadOp_Clear;
				att.clearValue = { 0.0, 0.0, 0.0, 1.0 };
			}
		} else {
			att.loadOp = WGPULoadOp_Load;
			att.storeOp = WGPUStoreOp_Store;
		}

		color_attachments.push_back(att);
	}

	// --- Build depth/stencil attachment ---
	WGPURenderPassDepthStencilAttachment ds_att = {};
	WGPURenderPassDepthStencilAttachment *ds_att_ptr = nullptr;

	const RDD::AttachmentReference &ds_ref = subpass.depth_stencil_reference;
	if (ds_ref.attachment != RDD::AttachmentReference::UNUSED &&
			ds_ref.attachment < fb->attachment_views.size()) {
		ds_att.view = fb->attachment_views[ds_ref.attachment];

		if (ds_ref.attachment < rp->attachments.size()) {
			const RDD::Attachment &attach_desc = rp->attachments[ds_ref.attachment];
			WGPUTextureFormat wgpu_fmt = _data_format_to_wgpu(attach_desc.format);
			bool has_depth = is_depth_format_wgpu(wgpu_fmt);
			bool has_stencil = has_stencil_wgpu(wgpu_fmt);

			if (has_depth) {
				ds_att.depthLoadOp = map_load_op(attach_desc.load_op);
				// Force Store for multi-subpass passes (see color attachment comment).
				if (rp->subpasses.size() > 1) {
					ds_att.depthStoreOp = WGPUStoreOp_Store;
				} else {
					ds_att.depthStoreOp = map_store_op(attach_desc.store_op);
				}
				if (ds_att.depthLoadOp == WGPULoadOp_Clear && ds_ref.attachment < p_clear_values.size()) {
					ds_att.depthClearValue = p_clear_values[ds_ref.attachment].depth;
				} else {
					ds_att.depthClearValue = 1.0f;
				}
			} else {
				ds_att.depthLoadOp = WGPULoadOp_Undefined;
				ds_att.depthStoreOp = WGPUStoreOp_Undefined;
				ds_att.depthReadOnly = true;
			}

			if (has_stencil) {
				ds_att.stencilLoadOp = map_load_op(attach_desc.stencil_load_op);
				// Force Store for multi-subpass passes (see color attachment comment).
				if (rp->subpasses.size() > 1) {
					ds_att.stencilStoreOp = WGPUStoreOp_Store;
				} else {
					ds_att.stencilStoreOp = map_store_op(attach_desc.stencil_store_op);
				}
				if (ds_att.stencilLoadOp == WGPULoadOp_Clear && ds_ref.attachment < p_clear_values.size()) {
					ds_att.stencilClearValue = p_clear_values[ds_ref.attachment].stencil;
				}
			} else {
				ds_att.stencilLoadOp = WGPULoadOp_Undefined;
				ds_att.stencilStoreOp = WGPUStoreOp_Undefined;
				ds_att.stencilReadOnly = true;
			}
		} else {
			// Fallback: load and store everything.
			ds_att.depthLoadOp = WGPULoadOp_Load;
			ds_att.depthStoreOp = WGPUStoreOp_Store;
			ds_att.depthClearValue = 1.0f;
			ds_att.stencilLoadOp = WGPULoadOp_Load;
			ds_att.stencilStoreOp = WGPUStoreOp_Store;
		}
		ds_att_ptr = &ds_att;
	}

	// --- Begin render pass ---
	// --- Diagnostic: verify swap chain view maps to correct JS object ---
#ifdef WEBGPU_VERBOSE
	if (rp->is_swap_chain_pass && color_attachments.size() > 0) {
		static int _sc_view_log = 0;
		if (_sc_view_log < 5) {
			WGPUTextureView view = color_attachments[0].view;
			int load_op_int = (int)color_attachments[0].loadOp;
			int store_op_int = (int)color_attachments[0].storeOp;
			EM_ASM({
				var viewPtr = $0;
				var jsView = WebGPU.getJsObject(viewPtr);
				var viewType = jsView ? jsView.constructor.name : 'null';
				var viewLabel = jsView ? jsView.label : 'N/A';
				console.log('[SC-VIEW#' + $3 + '] viewPtr=' + viewPtr +
					' jsType=' + viewType +
					' label=' + viewLabel +
					' loadOp=' + $1 + ' storeOp=' + $2);
				// Check if the view's texture matches the canvas surface texture.
				var canvas = document.querySelector('#canvas');
				if (canvas) {
					var ctx = canvas.getContext('webgpu');
					if (ctx) {
						try {
							var surfTex = ctx.getCurrentTexture();
							var surfView = surfTex.createView();
							console.log('[SC-VIEW#' + $3 + '] surfaceTex=' + surfTex.width + 'x' + surfTex.height +
								' fmt=' + surfTex.format + ' usage=' + surfTex.usage);
						} catch (e) {
							console.log('[SC-VIEW#' + $3 + '] getCurrentTexture error: ' + e.message);
						}
					}
				} }, (int)(uintptr_t)view, load_op_int, store_op_int, _sc_view_log);
			_sc_view_log++;
		}
	}
#endif // WEBGPU_VERBOSE

	WGPURenderPassDescriptor pass_desc = {};
	pass_desc.colorAttachmentCount = color_attachments.size();
	pass_desc.colorAttachments = color_attachments.ptr();
	pass_desc.depthStencilAttachment = ds_att_ptr;

	cmd->render_encoder = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &pass_desc);
	cmd->active_encoder = WGCommandBuffer::RENDER;

	// Reset cached state — new render pass requires binding everything fresh.
	cmd->render_state.current_pipeline = nullptr;
	cmd->render_state.current_index_buffer = nullptr;
	cmd->render_state.current_index_offset = 0;
	memset(cmd->render_state.current_vertex_buffers, 0, sizeof(cmd->render_state.current_vertex_buffers));
	memset(cmd->render_state.current_vertex_offsets, 0, sizeof(cmd->render_state.current_vertex_offsets));
}

void RenderingDeviceDriverWebGPU::command_end_render_pass(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// Temporary: log render pass ends to track pass boundaries.
	static int _rp_end_log = 0;
	if (_rp_end_log < 30) {
		WEBGPU_DIAG({ console.log('[RP-END#' + $0 + '] ' + $1 + 'x' + $2); }, _rp_end_log, cmd->render_state.render_area_width, cmd->render_state.render_area_height);
		_rp_end_log++;
	}

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderEnd(cmd->render_encoder);
		wgpuRenderPassEncoderRelease(cmd->render_encoder);
		cmd->render_encoder = nullptr;
	}
	cmd->active_encoder = WGCommandBuffer::NONE;
	cmd->render_state = {};
}

void RenderingDeviceDriverWebGPU::command_next_render_subpass(CommandBufferID p_cmd_buffer, CommandBufferType p_cmd_buffer_type) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// End current render pass encoder.
	if (cmd->render_encoder) {
		wgpuRenderPassEncoderEnd(cmd->render_encoder);
		wgpuRenderPassEncoderRelease(cmd->render_encoder);
		cmd->render_encoder = nullptr;
	}

	// Advance to next subpass.
	cmd->render_state.current_subpass++;

	WGRenderPass *rp = cmd->render_state.render_pass;
	WGFramebuffer *fb = cmd->render_state.framebuffer;
	ERR_FAIL_NULL(rp);
	ERR_FAIL_NULL(fb);
	ERR_FAIL_COND(cmd->render_state.current_subpass >= rp->subpasses.size());

	const WGRenderPass::SubpassInfo &subpass = rp->subpasses[cmd->render_state.current_subpass];

	// --- Build color attachments for the new subpass ---
	// For subsequent subpasses, we LOAD (not clear) color/depth since we want to preserve
	// what was drawn in previous subpasses on shared attachments.
	LocalVector<WGPURenderPassColorAttachment> color_attachments;
	for (uint32_t i = 0; i < subpass.color_references.size(); i++) {
		const RDD::AttachmentReference &ref = subpass.color_references[i];

		WGPURenderPassColorAttachment att = {};
		att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

		if (ref.attachment == RDD::AttachmentReference::UNUSED) {
			att.view = nullptr;
			att.loadOp = WGPULoadOp_Load;
			att.storeOp = WGPUStoreOp_Discard;
			color_attachments.push_back(att);
			continue;
		}

		if (ref.attachment < fb->attachment_views.size()) {
			att.view = fb->attachment_views[ref.attachment];
		}

		// Resolve target (MSAA).
		if (i < subpass.resolve_references.size()) {
			const RDD::AttachmentReference &res_ref = subpass.resolve_references[i];
			if (res_ref.attachment != RDD::AttachmentReference::UNUSED &&
					res_ref.attachment < fb->attachment_views.size()) {
				att.resolveTarget = fb->attachment_views[res_ref.attachment];
			}
		}

		// Load from previous subpass output, store for next use.
		att.loadOp = WGPULoadOp_Load;
		att.storeOp = WGPUStoreOp_Store;

		color_attachments.push_back(att);
	}

	// --- Build depth/stencil attachment ---
	WGPURenderPassDepthStencilAttachment ds_att = {};
	WGPURenderPassDepthStencilAttachment *ds_att_ptr = nullptr;

	const RDD::AttachmentReference &ds_ref = subpass.depth_stencil_reference;
	if (ds_ref.attachment != RDD::AttachmentReference::UNUSED &&
			ds_ref.attachment < fb->attachment_views.size()) {
		ds_att.view = fb->attachment_views[ds_ref.attachment];

		if (ds_ref.attachment < rp->attachments.size()) {
			const RDD::Attachment &attach_desc = rp->attachments[ds_ref.attachment];
			WGPUTextureFormat wgpu_fmt = _data_format_to_wgpu(attach_desc.format);
			bool has_depth = is_depth_format_wgpu(wgpu_fmt);
			bool has_stencil = has_stencil_wgpu(wgpu_fmt);

			if (has_depth) {
				ds_att.depthLoadOp = WGPULoadOp_Load;
				ds_att.depthStoreOp = WGPUStoreOp_Store;
				ds_att.depthClearValue = 0.0f;
			} else {
				ds_att.depthLoadOp = WGPULoadOp_Undefined;
				ds_att.depthStoreOp = WGPUStoreOp_Undefined;
				ds_att.depthReadOnly = true;
			}

			if (has_stencil) {
				ds_att.stencilLoadOp = WGPULoadOp_Load;
				ds_att.stencilStoreOp = WGPUStoreOp_Store;
			} else {
				ds_att.stencilLoadOp = WGPULoadOp_Undefined;
				ds_att.stencilStoreOp = WGPUStoreOp_Undefined;
				ds_att.stencilReadOnly = true;
			}
		} else {
			ds_att.depthLoadOp = WGPULoadOp_Load;
			ds_att.depthStoreOp = WGPUStoreOp_Store;
			ds_att.depthClearValue = 1.0f;
			ds_att.stencilLoadOp = WGPULoadOp_Load;
			ds_att.stencilStoreOp = WGPUStoreOp_Store;
		}
		ds_att_ptr = &ds_att;
	}

	// --- Begin new render pass for this subpass ---
	WGPURenderPassDescriptor pass_desc = {};
	pass_desc.colorAttachmentCount = color_attachments.size();
	pass_desc.colorAttachments = color_attachments.ptr();
	pass_desc.depthStencilAttachment = ds_att_ptr;

	cmd->render_encoder = wgpuCommandEncoderBeginRenderPass(cmd->encoder, &pass_desc);
	cmd->active_encoder = WGCommandBuffer::RENDER;

	// Reset pipeline state — new render pass requires re-binding everything.
	cmd->render_state.current_pipeline = nullptr;
	cmd->render_state.current_index_buffer = nullptr;
	cmd->render_state.current_index_offset = 0;
	memset(cmd->render_state.current_vertex_buffers, 0, sizeof(cmd->render_state.current_vertex_buffers));
	memset(cmd->render_state.current_vertex_offsets, 0, sizeof(cmd->render_state.current_vertex_offsets));
}

void RenderingDeviceDriverWebGPU::command_render_set_viewport(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_viewports) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (p_viewports.size() > 0) {
		const Rect2i &vp = p_viewports[0]; // WebGPU supports only one viewport.
		wgpuRenderPassEncoderSetViewport(cmd->render_encoder, (float)vp.position.x, (float)vp.position.y, (float)vp.size.x, (float)vp.size.y, 0.0f, 1.0f);
	}
}

void RenderingDeviceDriverWebGPU::command_render_set_scissor(CommandBufferID p_cmd_buffer, VectorView<Rect2i> p_scissors) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (p_scissors.size() > 0) {
		const Rect2i &sr = p_scissors[0];
		uint32_t x = MAX(sr.position.x, 0);
		uint32_t y = MAX(sr.position.y, 0);
		uint32_t w = MAX(sr.size.x, 0);
		uint32_t h = MAX(sr.size.y, 0);
		// Clamp scissor to the SMALLER of (render area) and (actual framebuffer size).
		// WebGPU requires scissor to fit within attachment dimensions.
		uint32_t clamp_w = 0;
		uint32_t clamp_h = 0;
		if (cmd->render_state.render_area_width > 0) {
			clamp_w = cmd->render_state.render_area_width;
			clamp_h = cmd->render_state.render_area_height;
		}
		if (cmd->render_state.framebuffer) {
			uint32_t fb_w = cmd->render_state.framebuffer->width;
			uint32_t fb_h = cmd->render_state.framebuffer->height;
			// Use actual attachment texture dimensions when available — fb->width/height
			// may reflect the Godot-level render-area (window size) rather than the GPU
			// texture size, which is what WebGPU validates the scissor rect against.
			const WGFramebuffer *wgfb = cmd->render_state.framebuffer;
			if (!wgfb->attachments.is_empty() && wgfb->attachments[0] != nullptr) {
				fb_w = wgfb->attachments[0]->width;
				fb_h = wgfb->attachments[0]->height;
			}
			clamp_w = clamp_w > 0 ? MIN(clamp_w, fb_w) : fb_w;
			clamp_h = clamp_h > 0 ? MIN(clamp_h, fb_h) : fb_h;
		}
		if (clamp_w > 0 && clamp_h > 0) {
			if (x >= clamp_w || y >= clamp_h) {
				w = 0;
				h = 0;
				x = 0;
				y = 0;
			} else {
				w = MIN(w, clamp_w - x);
				h = MIN(h, clamp_h - y);
			}
		}
		wgpuRenderPassEncoderSetScissorRect(cmd->render_encoder, x, y, w, h);
	}
}

void RenderingDeviceDriverWebGPU::command_render_clear_attachments(CommandBufferID p_cmd_buffer, VectorView<AttachmentClear> p_attachment_clears, VectorView<Rect2i> p_rects) {
	// WebGPU has no mid-pass clear. Options:
	// (a) End pass, do a clear pass, restart — simplest.
	// (b) Draw full-screen quad with clear color.
	// TODO: Implement option (a).
	WARN_PRINT_ONCE("WebGPU: command_render_clear_attachments not yet implemented.");
}

void RenderingDeviceDriverWebGPU::command_bind_render_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(pw);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline == pw) {
		return; // Pipeline already bound, skip redundant call.
	}

	wgpuRenderPassEncoderSetPipeline(cmd->render_encoder, pw->render_handle);
	wgpuRenderPassEncoderSetStencilReference(cmd->render_encoder, pw->stencil_reference);
	cmd->render_state.current_pipeline = pw;
	perf.set_pipeline_calls++;

	// Pre-bind empty bind groups at pipeline layout gap slots.
	// Firefox/wgpu requires all bind group indices to be set before draw calls.
	if (pw->shader) {
		for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
			wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, gap_idx, empty_bind_group, 0, nullptr);
			perf.gap_bind_group_calls++;
		}
	}
}

void RenderingDeviceDriverWebGPU::command_bind_render_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGShader *pipeline_shader = cmd->render_state.current_pipeline ? cmd->render_state.current_pipeline->shader : nullptr;

	// Diagnostic: log texture bindings and push constant info on swap chain pass.
	// Invalidate bind group tracking if the pipeline shader changed.
	if (pipeline_shader != cmd->bound_shader) {
		cmd->invalidate_bind_groups();
		cmd->bound_shader = pipeline_shader;
	}

	// Diagnostic: detect the sync-scope conflict that's causing the
	// "includes writable usage and another usage in the same synchronization
	// scope" validation error. This fires whenever a bound texture's parent
	// matches a framebuffer attachment's parent. Limited to a few prints so
	// we don't spam the console after a match.
	static int _sync_conflict_log_count = 0;
	WGFramebuffer *_cur_fb = cmd->render_state.framebuffer;
	if (_cur_fb && _sync_conflict_log_count < 20) {
		for (uint32_t i = 0; i < p_set_count; i++) {
			WGUniformSet *us = (WGUniformSet *)(p_uniform_sets[i].id);
			if (!us) {
				continue;
			}
			for (const KeyValue<uint32_t, WGTexture *> &kv : us->bound_textures) {
				WGTexture *btex = kv.value;
				if (!btex || !btex->view_source) {
					continue;
				}
				for (uint32_t a = 0; a < _cur_fb->attachments.size(); a++) {
					WGTexture *atex = _cur_fb->attachments[a];
					if (!atex) {
						continue;
					}
					WGPUTexture a_src = atex->gpu_handle();
					if (a_src == btex->view_source) {
						_sync_conflict_log_count++;
						if (_sync_conflict_log_count >= 20) {
							break;
						}
					}
				}
				if (_sync_conflict_log_count >= 20) {
					break;
				}
			}
			if (_sync_conflict_log_count >= 20) {
				break;
			}
		}
	}

	// Task 7.5: Unpack 4-bit frame indices from p_dynamic_offsets as we walk the sets.
	// Every set with `us->dynamic_buffers.size()` entries consumes that many 4-bit
	// slots in the mask (in binding order, matching uniform_sets_get_dynamic_offsets).
	static constexpr uint32_t MAX_DYNAMIC_BUFFERS = 8;
	uint32_t dyn_shift = 0;

	for (uint32_t i = 0; i < p_set_count; i++) {
		WGUniformSet *us = (WGUniformSet *)(p_uniform_sets[i].id);
		if (us && us->handle) {
			uint32_t set_idx = p_first_set_index + i;

			// Get a bind group compatible with the current pipeline's BGL.
			WGPUBindGroup bg_to_bind = _get_compatible_bind_group(us, pipeline_shader, set_idx);

			// Unpack dynamic offsets for this set's material dynamic buffers, in binding order.
			uint32_t set_dyn_offsets[MAX_DYNAMIC_BUFFERS] = {};
			uint32_t num_dyn = us->dynamic_buffers.size();
			if (num_dyn > MAX_DYNAMIC_BUFFERS) {
				num_dyn = MAX_DYNAMIC_BUFFERS;
			}
			for (uint32_t j = 0; j < num_dyn; j++) {
				uint32_t frame_idx = (p_dynamic_offsets >> dyn_shift) & 0xFu;
				dyn_shift += 4u;
				const WGBuffer *dbuf = us->dynamic_buffers[j];
				set_dyn_offsets[j] = (uint32_t)(frame_idx * (dbuf ? dbuf->per_frame_size : 0));
			}

			const bool is_pc_merged = (pipeline_shader && pipeline_shader->push_constant_size > 0 &&
					set_idx == pipeline_shader->push_constant_bind_group &&
					pipeline_shader->merged_pc_group_layout != nullptr);

			if (is_pc_merged) {
				// PC ring buffer is at PUSH_CONSTANT_RING_BINDING (120) — the highest
				// binding in the merged group, so its dynamic offset is appended last.
				// Save the material offsets so _flush_push_constants can preserve them
				// while patching in the real PC ring offset before each draw.
				cmd->pc_group_material_dyn_count = num_dyn;
				for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_PC_GROUP_MATERIAL_DYN; j++) {
					cmd->pc_group_material_dyn_offsets[j] = set_dyn_offsets[j];
				}
				// Append last-flushed PC ring offset so the draw is correct even if
				// _flush_push_constants doesn't fire (e.g. firstInstance optimization).
				if (num_dyn < MAX_DYNAMIC_BUFFERS) {
					set_dyn_offsets[num_dyn] = cmd->last_flushed_pc_offset;
					num_dyn++;
				}
				cmd->current_pc_bind_group = bg_to_bind;
				wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, set_idx, bg_to_bind, num_dyn, set_dyn_offsets);
				perf.set_bind_group_calls++;
				// Save full state for mid-pass restart.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
					auto &bs = cmd->last_bound_state[set_idx];
					bs.group = bg_to_bind;
					bs.dynamic_offset_count = num_dyn;
					for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_BIND_GROUP_DYN_OFFSETS; j++) {
						bs.dynamic_offsets[j] = set_dyn_offsets[j];
					}
				}
				// PC group always rebinds per draw (via _flush_push_constants); don't cache.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
					cmd->bound_bind_groups[set_idx] = nullptr;
				}
			} else if (pipeline_shader && pipeline_shader->push_constant_size > 0 &&
					set_idx == pipeline_shader->push_constant_bind_group) {
				// PC group, but shader has no merged layout → just clear the PC pointer.
				cmd->current_pc_bind_group = nullptr;
				cmd->pc_group_material_dyn_count = 0;
				wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, set_idx, bg_to_bind, num_dyn, num_dyn ? set_dyn_offsets : nullptr);
				perf.set_bind_group_calls++;
				// Save full state for mid-pass restart.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
					auto &bs = cmd->last_bound_state[set_idx];
					bs.group = bg_to_bind;
					bs.dynamic_offset_count = num_dyn;
					for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_BIND_GROUP_DYN_OFFSETS; j++) {
						bs.dynamic_offsets[j] = set_dyn_offsets[j];
					}
					cmd->bound_bind_groups[set_idx] = num_dyn > 0 ? nullptr : bg_to_bind;
				}
			} else if (num_dyn > 0) {
				// Non-PC set with material dynamic buffers: must always rebind because
				// the frame_idx rotates — bypass the redundant-bind cache.
				wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, set_idx, bg_to_bind, num_dyn, set_dyn_offsets);
				perf.set_bind_group_calls++;
				// Save full state for mid-pass restart.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
					auto &bs = cmd->last_bound_state[set_idx];
					bs.group = bg_to_bind;
					bs.dynamic_offset_count = num_dyn;
					for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_BIND_GROUP_DYN_OFFSETS; j++) {
						bs.dynamic_offsets[j] = set_dyn_offsets[j];
					}
					cmd->bound_bind_groups[set_idx] = nullptr;
				}
			} else {
				// Static non-PC set — skip if already bound.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS && cmd->bound_bind_groups[set_idx] == bg_to_bind) {
					continue;
				}
				wgpuRenderPassEncoderSetBindGroup(cmd->render_encoder, set_idx, bg_to_bind, 0, nullptr);
				perf.set_bind_group_calls++;
				// Save full state for mid-pass restart.
				if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
					auto &bs = cmd->last_bound_state[set_idx];
					bs.group = bg_to_bind;
					bs.dynamic_offset_count = 0;
					cmd->bound_bind_groups[set_idx] = bg_to_bind;
				}
			}
		}
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw(CommandBufferID p_cmd_buffer, uint32_t p_vertex_count, uint32_t p_instance_count, uint32_t p_base_vertex, uint32_t p_first_instance) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	perf.draw_calls++;
	if (p_first_instance > 0) {
		perf.first_instance_draws++;
	}
	wgpuRenderPassEncoderDraw(cmd->render_encoder, p_vertex_count, p_instance_count, p_base_vertex, p_first_instance);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed(CommandBufferID p_cmd_buffer, uint32_t p_index_count, uint32_t p_instance_count, uint32_t p_first_index, int32_t p_vertex_offset, uint32_t p_first_instance) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPipelineWrapper *pw = cmd->render_state.current_pipeline;
	if (pw) {
		_flush_push_constants(cmd, pw->shader);
		// For strip topologies, ensure the pipeline variant matches the bound index format.
		// Always select the correct variant — the format may have changed since pipeline bind.
		if (pw->is_strip && pw->render_handle_u16) {
			WGPURenderPipeline needed = (cmd->render_state.current_index_format == WGPUIndexFormat_Uint16)
					? pw->render_handle_u16
					: pw->render_handle;
			wgpuRenderPassEncoderSetPipeline(cmd->render_encoder, needed);
		}
	}

	perf.draw_calls++;
	if (p_first_instance > 0) {
		perf.first_instance_draws++;
	}
	wgpuRenderPassEncoderDrawIndexed(cmd->render_encoder, p_index_count, p_instance_count, p_first_index, p_vertex_offset, p_first_instance);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPipelineWrapper *pw = cmd->render_state.current_pipeline;
	if (pw) {
		_flush_push_constants(cmd, pw->shader);
		if (pw->is_strip && pw->render_handle_u16) {
			WGPURenderPipeline needed = (cmd->render_state.current_index_format == WGPUIndexFormat_Uint16)
					? pw->render_handle_u16
					: pw->render_handle;
			wgpuRenderPassEncoderSetPipeline(cmd->render_encoder, needed);
		}
	}

	// WebGPU has no multi-draw-indirect — must loop.
	for (uint32_t i = 0; i < p_draw_count; i++) {
		wgpuRenderPassEncoderDrawIndexedIndirect(cmd->render_encoder, indirect->handle, p_offset + i * p_stride);
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw_indexed_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) {
	// TODO: Read count from buffer (requires async readback). For now, use max count.
	command_render_draw_indexed_indirect(p_cmd_buffer, p_indirect_buffer, p_offset, p_max_draw_count, p_stride);
}

void RenderingDeviceDriverWebGPU::command_render_draw_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->render_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	for (uint32_t i = 0; i < p_draw_count; i++) {
		wgpuRenderPassEncoderDrawIndirect(cmd->render_encoder, indirect->handle, p_offset + i * p_stride);
	}
}

void RenderingDeviceDriverWebGPU::command_render_draw_indirect_count(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset, BufferID p_count_buffer, uint64_t p_count_buffer_offset, uint32_t p_max_draw_count, uint32_t p_stride) {
	command_render_draw_indirect(p_cmd_buffer, p_indirect_buffer, p_offset, p_max_draw_count, p_stride);
}

void RenderingDeviceDriverWebGPU::command_render_bind_vertex_buffers(CommandBufferID p_cmd_buffer, uint32_t p_binding_count, const BufferID *p_buffers, const uint64_t *p_offsets, uint64_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	for (uint32_t i = 0; i < p_binding_count; i++) {
		WGBuffer *buf = (WGBuffer *)(p_buffers[i].id);
		if (buf) {
			uint64_t offset = p_offsets[i];
			if (buf->is_dynamic()) {
				uint64_t frame_idx = p_dynamic_offsets & 0x3;
				p_dynamic_offsets >>= 2;
				offset += frame_idx * buf->per_frame_size;
			}
			// Skip redundant SetVertexBuffer calls.
			if (i < WGCommandBuffer::RenderState::MAX_VERTEX_BINDINGS &&
					cmd->render_state.current_vertex_buffers[i] == buf->handle &&
					cmd->render_state.current_vertex_offsets[i] == offset) {
				continue;
			}
			wgpuRenderPassEncoderSetVertexBuffer(cmd->render_encoder, i, buf->handle, offset, WGPU_WHOLE_SIZE);
			perf.set_vertex_buffer_calls++;
			if (i < WGCommandBuffer::RenderState::MAX_VERTEX_BINDINGS) {
				cmd->render_state.current_vertex_buffers[i] = buf->handle;
				cmd->render_state.current_vertex_offsets[i] = offset;
			}
		}
	}
}

void RenderingDeviceDriverWebGPU::command_render_bind_index_buffer(CommandBufferID p_cmd_buffer, BufferID p_buffer, IndexBufferFormat p_format, uint64_t p_offset) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *buf = (WGBuffer *)(p_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(buf);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPUIndexFormat format = (p_format == INDEX_BUFFER_FORMAT_UINT16) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;

	if (cmd->render_state.current_index_buffer == buf->handle &&
			cmd->render_state.current_index_offset == p_offset &&
			cmd->render_state.current_index_format == format) {
		return; // Index buffer already bound, skip redundant call.
	}

	wgpuRenderPassEncoderSetIndexBuffer(cmd->render_encoder, buf->handle, format, p_offset, WGPU_WHOLE_SIZE);
	cmd->render_state.current_index_buffer = buf->handle;
	cmd->render_state.current_index_offset = p_offset;
	cmd->render_state.current_index_format = format;
}

void RenderingDeviceDriverWebGPU::command_render_set_blend_constants(CommandBufferID p_cmd_buffer, const Color &p_constants) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->render_encoder);

	WGPUColor color = { p_constants.r, p_constants.g, p_constants.b, p_constants.a };
	wgpuRenderPassEncoderSetBlendConstant(cmd->render_encoder, &color);
}

void RenderingDeviceDriverWebGPU::command_render_set_line_width(CommandBufferID p_cmd_buffer, float p_width) {
	// No-op: WebGPU doesn't support line width > 1.0.
}

// =============================================================================
// Specialization Constant Support
// =============================================================================

// SPIR-V opcodes for specialization constant handling.
static constexpr uint16_t SPV_OP_DECORATE = 71;
static constexpr uint16_t SPV_OP_SPEC_CONSTANT_TRUE = 48;
static constexpr uint16_t SPV_OP_SPEC_CONSTANT_FALSE = 49;
static constexpr uint16_t SPV_OP_SPEC_CONSTANT = 50;
static constexpr uint16_t SPV_DECORATION_SPEC_ID = 1;

static inline uint32_t _spv_read_word(const uint8_t *p_data, uint32_t p_word_index) {
	const uint8_t *p = p_data + p_word_index * 4;
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static inline void _spv_write_word(uint8_t *p_data, uint32_t p_word_index, uint32_t p_value) {
	uint8_t *p = p_data + p_word_index * 4;
	p[0] = p_value & 0xFF;
	p[1] = (p_value >> 8) & 0xFF;
	p[2] = (p_value >> 16) & 0xFF;
	p[3] = (p_value >> 24) & 0xFF;
}

// Patches SPIR-V bytecode to apply specialization constant values.
// Returns a modified copy with OpSpecConstant* values updated.
static PackedByteArray _patch_spirv_spec_constants(const PackedByteArray &p_spirv, VectorView<RDD::PipelineSpecializationConstant> p_constants) {
	if (p_constants.size() == 0 || p_spirv.size() < 20) {
		return p_spirv;
	}

	// Build a map: SpecId → value (as uint32_t).
	HashMap<uint32_t, uint32_t> spec_values;
	for (uint32_t i = 0; i < p_constants.size(); i++) {
		const RDD::PipelineSpecializationConstant &c = p_constants[i];
		uint32_t val = 0;
		switch (c.type) {
			case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
				val = c.bool_value ? 1 : 0;
				break;
			case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
				val = (uint32_t)c.int_value;
				break;
			case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
				memcpy(&val, &c.float_value, sizeof(float));
				break;
		}
		spec_values[c.constant_id] = val;
	}

	// First pass: scan OpDecorate for SpecId → result_id mapping.
	HashMap<uint32_t, uint32_t> spec_id_to_result_id; // SpecId → result_id
	uint32_t total_words = p_spirv.size() / 4;
	uint32_t pos = 5; // Skip SPIR-V header (5 words).
	while (pos < total_words) {
		uint32_t w0 = _spv_read_word(p_spirv.ptr(), pos);
		uint32_t wc = w0 >> 16;
		uint32_t op = w0 & 0xFFFF;
		if (wc == 0 || pos + wc > total_words) {
			break;
		}
		// OpDecorate target decoration [literal...]
		// For SpecId: OpDecorate result_id SpecId(1) literal_spec_id
		if (op == SPV_OP_DECORATE && wc >= 4) {
			uint32_t target = _spv_read_word(p_spirv.ptr(), pos + 1);
			uint32_t decoration = _spv_read_word(p_spirv.ptr(), pos + 2);
			if (decoration == SPV_DECORATION_SPEC_ID) {
				uint32_t spec_id = _spv_read_word(p_spirv.ptr(), pos + 3);
				spec_id_to_result_id[spec_id] = target;
			}
		}
		pos += wc;
	}

	// Build result_id → value map.
	HashMap<uint32_t, uint32_t> result_to_value;
	for (const KeyValue<uint32_t, uint32_t> &kv : spec_id_to_result_id) {
		if (spec_values.has(kv.key)) {
			result_to_value[kv.value] = spec_values[kv.key];
		}
	}

	if (result_to_value.is_empty()) {
		return p_spirv; // No matching spec constants to patch.
	}

	// Second pass: patch the SPIR-V.
	PackedByteArray out = p_spirv;
	pos = 5;
	while (pos < total_words) {
		uint32_t w0 = _spv_read_word(out.ptr(), pos);
		uint32_t wc = w0 >> 16;
		uint32_t op = w0 & 0xFFFF;
		if (wc == 0 || pos + wc > total_words) {
			break;
		}

		if (op == SPV_OP_SPEC_CONSTANT_TRUE || op == SPV_OP_SPEC_CONSTANT_FALSE) {
			// OpSpecConstantTrue/False: wc=3, [type_id, result_id]
			if (wc >= 3) {
				uint32_t result_id = _spv_read_word(out.ptr(), pos + 2);
				if (result_to_value.has(result_id)) {
					uint32_t val = result_to_value[result_id];
					uint16_t new_op = val ? SPV_OP_SPEC_CONSTANT_TRUE : SPV_OP_SPEC_CONSTANT_FALSE;
					_spv_write_word(out.ptrw(), pos, (wc << 16) | new_op);
				}
			}
		} else if (op == SPV_OP_SPEC_CONSTANT) {
			// OpSpecConstant: wc>=4, [type_id, result_id, value...]
			if (wc >= 4) {
				uint32_t result_id = _spv_read_word(out.ptr(), pos + 2);
				if (result_to_value.has(result_id)) {
					_spv_write_word(out.ptrw(), pos + 3, result_to_value[result_id]);
				}
			}
		}

		pos += wc;
	}

	return out;
}

// Creates a WGPUShaderModule from SPIR-V with specialization constants applied.
// Returns nullptr on failure.
WGPUShaderModule RenderingDeviceDriverWebGPU::_create_module_with_spec_constants(
		const PackedByteArray &p_spirv,
		VectorView<PipelineSpecializationConstant> p_constants,
		ShaderStage p_stage,
		const HashMap<uint32_t, uint32_t> &p_rw_storage_splits) {
	PackedByteArray patched = _patch_spirv_spec_constants(p_spirv, p_constants);

	// Cached SPIR-V → WGSL via Tint (see _spv_to_wgsl_cached above).
	char *wgsl_str = _spv_to_wgsl_cached(patched.ptr(), (int)patched.size());

	if (!wgsl_str) {
		ERR_PRINT("WebGPU: SPIR-V→WGSL conversion failed for specialized shader module.");
		return nullptr;
	}

	// Format remapping + vertex-stage read_write demotion -- see
	// _remap_unsupported_wgsl_storage_formats()'s doc comment; shared with
	// shader_create_from_container()'s own copy of this same logic.
	_remap_unsupported_wgsl_storage_formats(wgsl_str, p_stage);

	// Split read_write storage textures into write + shadow-read bindings --
	// same transformation as shader_create_from_container()'s own copy of this
	// pass (see its doc comment, ~line 4481, for the full rationale), but
	// deliberately NOT a re-detection: this path re-runs Tint on a
	// specialization-patched copy of the SPIR-V, whose surviving dead-code
	// elimination can differ per specialization, so independently re-scanning
	// this module's WGSL for "read_write>" and re-deriving a fresh split
	// (as an earlier, reverted attempt at this fix did -- see Task 9.5 Round
	// 22's TASKS.md writeup) risks discovering a different set of bindings,
	// or the right binding via a differently-ordered scan, than what the
	// *primary* module's conversion already split when the shader's
	// BindGroupLayout was built (once, in shader_create_from_container(),
	// stored as shader->rw_storage_splits and passed in here via
	// p_rw_storage_splits) -- producing a specialized module whose declared
	// bindings don't match the BGL Dawn already validates it against. Instead,
	// only ever split a (set,binding) this module actually still declares as
	// read_write AND that the primary module already decided to split, using
	// the exact same shadow binding number the BGL was built with (not a
	// freshly recomputed one) -- guaranteeing the two always agree. Bindings
	// this specialization's own DCE has stripped entirely are simply absent
	// from the WGSL text and the scan below finds nothing for them, which is
	// correct: no binding, nothing to split.
	if (!has_rw_storage_textures && !p_rw_storage_splits.is_empty() && strstr(wgsl_str, "read_write>")) {
		struct RWSplitInfo {
			uint32_t grp, bnd, shadow_bnd;
			String var_name, dim_type, fmt;
		};
		LocalVector<RWSplitInfo> rw_infos;

		// Scan for texture_storage_*<fmt,read_write> declarations -- identical
		// detection logic to shader_create_from_container()'s own copy.
		{
			const char *p = wgsl_str;
			while ((p = strstr(p, "@group(")) != nullptr) {
				unsigned int grp = 0, bnd = 0;
				if (!parse_group_binding(p, grp, bnd)) {
					p++;
					continue;
				}
				const char *semi = strchr(p, ';');
				if (!semi) {
					p++;
					continue;
				}
				const char *rw = strstr(p, "read_write>");
				if (!rw || rw >= semi) {
					p = semi;
					continue;
				}
				// Only split bindings the primary module already split --
				// see the doc comment above for why this must not be a
				// blind re-detection.
				const uint32_t *shadow_bnd_ptr = p_rw_storage_splits.getptr(((uint32_t)grp << 16) | bnd);
				if (!shadow_bnd_ptr) {
					p = semi;
					continue;
				}
				const char *ts = strstr(p, "texture_storage_");
				if (!ts || ts >= semi) {
					p = semi;
					continue;
				}
				// Variable name: "var NAME:"
				const char *vp = strstr(p, "var ");
				if (!vp || vp > ts) {
					p = semi;
					continue;
				}
				vp += 4;
				const char *colon = strchr(vp, ':');
				if (!colon || colon > ts) {
					p = semi;
					continue;
				}
				const char *ne = colon;
				while (ne > vp && (*(ne - 1) == ' ' || *(ne - 1) == '\t')) {
					ne--;
				}
				if (ne <= vp) {
					p = semi;
					continue;
				}
				// Dim type: texture_storage_Xd
				const char *lt = strchr(ts, '<');
				if (!lt || lt > rw) {
					p = semi;
					continue;
				}
				// Format: between < and , before read_write
				const char *comma = strchr(lt + 1, ',');
				if (!comma || comma >= rw) {
					p = semi;
					continue;
				}
				int fmt_len = (int)(comma - lt - 1);
				if (fmt_len <= 0 || fmt_len >= 64) {
					p = semi;
					continue;
				}

				RWSplitInfo info;
				info.grp = grp;
				info.bnd = bnd;
				info.shadow_bnd = *shadow_bnd_ptr;
				{
					char buf[256];
					int len = (int)(ne - vp);
					if (len > 255) {
						len = 255;
					}
					memcpy(buf, vp, len);
					buf[len] = '\0';
					info.var_name = buf;
				}
				{
					char buf[256];
					int len = (int)(lt - ts);
					if (len > 255) {
						len = 255;
					}
					memcpy(buf, ts, len);
					buf[len] = '\0';
					info.dim_type = buf;
				}
				{
					char buf[64];
					if (fmt_len > 63) {
						fmt_len = 63;
					}
					memcpy(buf, lt + 1, fmt_len);
					buf[fmt_len] = '\0';
					info.fmt = String(buf).strip_edges();
				}
				rw_infos.push_back(info);
				p = semi;
			}
		}

		if (rw_infos.size() > 0) {
			String ws(wgsl_str);
			for (const RWSplitInfo &info : rw_infos) {
				uint32_t shadow_bnd = info.shadow_bnd;
				String shadow_name = info.var_name + "_rw_in";

				// 1. Replace read_write with write in the declaration.
				ws = ws.replace(
						info.dim_type + "<" + info.fmt + ",read_write>",
						info.dim_type + "<" + info.fmt + ",write>");
				ws = ws.replace(
						info.dim_type + "<" + info.fmt + ", read_write>",
						info.dim_type + "<" + info.fmt + ", write>");

				// 2. Insert shadow read binding as a sampled texture (not storage) --
				// same reasoning as the primary path's copy of this step.
				String write_pat = info.dim_type + "<" + info.fmt + ",write>;";
				int64_t wt_pos = ws.find(write_pat);
				if (wt_pos == -1) {
					write_pat = info.dim_type + "<" + info.fmt + ", write>;";
					wt_pos = ws.find(write_pat);
				}
				if (wt_pos != -1) {
					int64_t insert_pos = wt_pos + write_pat.length();
					String sampled_dim = info.dim_type.replace("texture_storage_", "texture_");
					String comp_type = "f32";
					if (info.fmt.find("uint") != -1) {
						comp_type = "u32";
					} else if (info.fmt.find("sint") != -1) {
						comp_type = "i32";
					}
					String shadow_decl = "\n@group(" + itos(info.grp) + ") @binding(" + itos(shadow_bnd) + ") \nvar " + shadow_name + ": " + sampled_dim + "<" + comp_type + ">;";
					ws = ws.substr(0, insert_pos) + shadow_decl + ws.substr(insert_pos);
				}

				// 3. Redirect textureLoad to the shadow (not textureStore).
				ws = ws.replace("textureLoad(" + info.var_name + ",", "textureLoad(" + shadow_name + ",");
				ws = ws.replace("textureLoad(" + info.var_name + ")", "textureLoad(" + shadow_name + ")");

				// 4. Sampled textures need a mip level parameter in textureLoad.
				{
					String search = "textureLoad(" + shadow_name;
					int64_t pos = 0;
					while ((pos = ws.find(search, pos)) != -1) {
						int64_t paren_start = pos + String("textureLoad").length();
						int depth = 0;
						int64_t insert_mip = -1;
						for (int64_t k = paren_start; k < ws.length(); k++) {
							if (ws[k] == '(') {
								depth++;
							} else if (ws[k] == ')') {
								depth--;
								if (depth == 0) {
									insert_mip = k;
									break;
								}
							}
						}
						if (insert_mip != -1) {
							ws = ws.substr(0, insert_mip) + ", 0" + ws.substr(insert_mip);
							pos = insert_mip + 3;
						} else {
							pos += search.length();
						}
					}
				}

				// Deliberately not touching wgsl_rw_storage_splits/wgsl_storage_tex_format
				// here -- those are shader_create_from_container()-local, and already
				// correctly populated once from the primary module; this path only
				// needs the specialized module's *text* to agree with the
				// already-built BGL, not to rebuild any of that metadata itself.
			}
			free(wgsl_str);
			CharString cs = ws.utf8();
			wgsl_str = (char *)malloc(cs.length() + 1);
			memcpy(wgsl_str, cs.get_data(), cs.length() + 1);
		}
	}

	// FLATTEN-BA fallback removed here too -- see the NOTE in
	// shader_create_from_container() above (Task 9.15/Tier 1 #4 Phase 2): dead
	// code, already superseded by spirv_preprocess::flatten_binding_arrays().

	// See _reclassify_single_component_depth_textures()'s doc comment (Task
	// 7.13); must match shader_create_from_container(). This path never
	// performs the read-only-storage-to-sampled conversion itself, so there's
	// no exclusion set to pass -- an empty set disables the exclusion check.
	static const HashSet<uint32_t> empty_storage_converted_keys;
	wgsl_str = _reclassify_single_component_depth_textures(wgsl_str, empty_storage_converted_keys);

	WGPUShaderSourceWGSL wgsl_source = {};
	wgsl_source.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgsl_source.code = { wgsl_str, WGPU_STRLEN };

	WGPUShaderModuleDescriptor desc = {};
	desc.nextInChain = &wgsl_source.chain;

	// Label the specialized module so the JS-side createShaderModule patch identifies it.
	static int _spec_mod_id = 0;
	int _specid = _spec_mod_id++;
	String _spec_label = "specmod#" + itos(_specid) + ":stg" + itos((int)p_stage);
	CharString _spec_label_cs = _spec_label.utf8();
	desc.label = { _spec_label_cs.get_data(), WGPU_STRLEN };

	WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &desc);
	free(wgsl_str);

	return mod;
}

RDD::PipelineID RenderingDeviceDriverWebGPU::render_pipeline_create(
		ShaderID p_shader,
		VertexFormatID p_vertex_format,
		RenderPrimitive p_render_primitive,
		PipelineRasterizationState p_rasterization_state,
		PipelineMultisampleState p_multisample_state,
		PipelineDepthStencilState p_depth_stencil_state,
		PipelineColorBlendState p_blend_state,
		VectorView<int32_t> p_color_attachments,
		BitField<PipelineDynamicStateFlags> p_dynamic_state,
		RenderPassID p_render_pass,
		uint32_t p_render_subpass,
		VectorView<PipelineSpecializationConstant> p_specialization_constants) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_COND_V(!shader, PipelineID());
	WGVertexFormat *vf = p_vertex_format.id ? (WGVertexFormat *)(p_vertex_format.id) : nullptr;
	WGRenderPass *rp = (WGRenderPass *)(p_render_pass.id);
	ERR_FAIL_COND_V(!rp, PipelineID());
	ERR_FAIL_COND_V(p_render_subpass >= (uint32_t)rp->subpasses.size(), PipelineID());
	const WGRenderPass::SubpassInfo &subpass = rp->subpasses[p_render_subpass];

	// --- Specialization constants ---
	// If the base shader modules were compiled with WGSL override declarations
	// (spirv_to_wgsl_with_overrides), we can pass specialization values as
	// pipeline constants instead of creating specialized shader modules.
	// This eliminates all runtime SPIR-V patching and Tint conversion.
	WGPUShaderModule vertex_module = shader->stage_modules[SHADER_STAGE_VERTEX];
	WGPUShaderModule fragment_module = shader->stage_modules[SHADER_STAGE_FRAGMENT];
	WGPUShaderModule specialized_vertex = nullptr;
	WGPUShaderModule specialized_fragment = nullptr;

	// Build per-stage WGPUConstantEntry arrays from specialization constants.
	// WebGPU requires that every constant key passed to a stage actually exists
	// as an @id(N) override in that stage's module — unlike Vulkan which silently
	// ignores unknown spec constant IDs. So we filter by stage_override_ids.
	LocalVector<WGPUConstantEntry> vertex_constants;
	LocalVector<WGPUConstantEntry> frag_constants;
	LocalVector<CharString> constant_key_storage; // keep key strings alive
	bool use_override_path = shader->has_override_declarations;
	if (p_specialization_constants.size() > 0) {
		if (use_override_path) {
			// Override path: pass constants at pipeline creation, reuse base modules.
			const HashSet<uint32_t> &vtx_ids = shader->stage_override_ids[SHADER_STAGE_VERTEX];
			const HashSet<uint32_t> &frg_ids = shader->stage_override_ids[SHADER_STAGE_FRAGMENT];
			for (uint32_t i = 0; i < p_specialization_constants.size(); i++) {
				const PipelineSpecializationConstant &sc = p_specialization_constants[i];
				bool in_vtx = vtx_ids.has(sc.constant_id);
				bool in_frg = frg_ids.has(sc.constant_id);
				if (!in_vtx && !in_frg) {
					continue; // This constant ID doesn't exist in any stage's WGSL.
				}
				WGPUConstantEntry entry = {};
				String key_str = itos(sc.constant_id);
				CharString key_cs = key_str.utf8();
				constant_key_storage.push_back(key_cs);
				entry.key = { constant_key_storage[constant_key_storage.size() - 1].get_data(), WGPU_STRLEN };
				switch (sc.type) {
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
						entry.value = sc.bool_value ? 1.0 : 0.0;
						break;
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
						entry.value = (double)sc.int_value;
						break;
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
						entry.value = (double)sc.float_value;
						break;
				}
				if (in_vtx) {
					vertex_constants.push_back(entry);
				}
				if (in_frg) {
					frag_constants.push_back(entry);
				}
			}
		} else {
			// Legacy path: patch SPIR-V and create specialized modules via Tint.
			if (!shader->stage_spirv[SHADER_STAGE_VERTEX].is_empty()) {
				specialized_vertex = _create_module_with_spec_constants(
						shader->stage_spirv[SHADER_STAGE_VERTEX], p_specialization_constants, SHADER_STAGE_VERTEX, shader->rw_storage_splits);
				if (specialized_vertex) {
					vertex_module = specialized_vertex;
				}
			}
			if (!shader->stage_spirv[SHADER_STAGE_FRAGMENT].is_empty()) {
				specialized_fragment = _create_module_with_spec_constants(
						shader->stage_spirv[SHADER_STAGE_FRAGMENT], p_specialization_constants, SHADER_STAGE_FRAGMENT, shader->rw_storage_splits);
				if (specialized_fragment) {
					fragment_module = specialized_fragment;
				}
			}
		}
	}

	// --- Vertex state ---
	LocalVector<WGPUVertexBufferLayout> vb_layouts;
	LocalVector<WGPUVertexAttribute> vb_attrs;
	if (vf && !vf->attributes.is_empty()) {
		// Group attributes by binding, counting totals first.
		HashMap<uint32_t, uint32_t> binding_attr_count;
		for (const WGVertexFormat::Attribute &a : vf->attributes) {
			binding_attr_count[a.binding]++;
		}
		uint32_t total_attrs = vf->attributes.size();
		vb_attrs.resize(total_attrs); // Pre-allocate — no reallocation after this.

		// Determine start index per binding.
		HashMap<uint32_t, uint32_t> binding_start;
		uint32_t pos = 0;
		for (uint32_t b = 0; b < vf->bindings.size(); b++) {
			if (binding_attr_count.has(b)) {
				binding_start[b] = pos;
				pos += binding_attr_count[b];
			}
		}

		// Fill attrs per binding.
		HashMap<uint32_t, uint32_t> fill_pos;
		for (const WGVertexFormat::Attribute &a : vf->attributes) {
			uint32_t idx = binding_start[a.binding] + (fill_pos.has(a.binding) ? fill_pos[a.binding]++ : (fill_pos[a.binding] = 1, 0));
			vb_attrs[idx].format = a.format;
			vb_attrs[idx].offset = a.offset;
			vb_attrs[idx].shaderLocation = a.location;
		}

		// (debug removed)

		// Build layouts (one per binding that has attributes).
		for (uint32_t b = 0; b < vf->bindings.size(); b++) {
			if (!binding_attr_count.has(b)) {
				continue;
			}
			WGPUVertexBufferLayout layout = {};
			layout.arrayStride = vf->bindings[b].stride;
			layout.stepMode = vf->bindings[b].step_mode;
			layout.attributeCount = binding_attr_count[b];
			layout.attributes = vb_attrs.ptr() + binding_start[b];
			vb_layouts.push_back(layout);
		}
	}

	WGPUVertexState vertex_state = {};
	vertex_state.module = vertex_module;
	vertex_state.entryPoint = { "main", WGPU_STRLEN };
	vertex_state.bufferCount = vb_layouts.size();
	vertex_state.buffers = vb_layouts.ptr();
	if (use_override_path && !vertex_constants.is_empty()) {
		vertex_state.constantCount = vertex_constants.size();
		vertex_state.constants = vertex_constants.ptr();
	}

	// --- Primitive state ---
	WGPUPrimitiveState primitive = {};
	bool is_strip = false;
	switch (p_render_primitive) {
		case RENDER_PRIMITIVE_POINTS:
			primitive.topology = WGPUPrimitiveTopology_PointList;
			break;
		case RENDER_PRIMITIVE_LINES:
		case RENDER_PRIMITIVE_LINES_WITH_ADJACENCY:
			primitive.topology = WGPUPrimitiveTopology_LineList;
			break;
		case RENDER_PRIMITIVE_LINESTRIPS:
		case RENDER_PRIMITIVE_LINESTRIPS_WITH_ADJACENCY:
			primitive.topology = WGPUPrimitiveTopology_LineStrip;
			is_strip = true;
			break;
		case RENDER_PRIMITIVE_TRIANGLES:
		case RENDER_PRIMITIVE_TRIANGLES_WITH_ADJACENCY:
			primitive.topology = WGPUPrimitiveTopology_TriangleList;
			break;
		case RENDER_PRIMITIVE_TRIANGLE_STRIPS:
		case RENDER_PRIMITIVE_TRIANGLE_STRIPS_WITH_AJACENCY:
		case RENDER_PRIMITIVE_TRIANGLE_STRIPS_WITH_RESTART_INDEX:
			primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
			is_strip = true;
			break;
		default:
			primitive.topology = WGPUPrimitiveTopology_TriangleList;
			break;
	}
	if (is_strip) {
		primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
	}
	switch (p_rasterization_state.cull_mode) {
		case POLYGON_CULL_FRONT:
			primitive.cullMode = WGPUCullMode_Front;
			break;
		case POLYGON_CULL_BACK:
			primitive.cullMode = WGPUCullMode_Back;
			break;
		default:
			primitive.cullMode = WGPUCullMode_None;
			break;
	}
	primitive.frontFace = (p_rasterization_state.front_face == POLYGON_FRONT_FACE_CLOCKWISE)
			? WGPUFrontFace_CW
			: WGPUFrontFace_CCW;

	// --- Multisample state ---
	WGPUMultisampleState multisample = {};
	// TextureSamples enum: 0=1x, 1=2x, 2=4x, 3=8x ... → 1 << sample_count
	multisample.count = (uint32_t)(1u << (uint32_t)p_multisample_state.sample_count);
	multisample.mask = 0xFFFFFFFFu;
	multisample.alphaToCoverageEnabled = p_multisample_state.enable_alpha_to_coverage;

	// --- Depth/stencil helpers ---
	auto map_compare = [](CompareOperator op) -> WGPUCompareFunction {
		switch (op) {
			case COMPARE_OP_NEVER:
				return WGPUCompareFunction_Never;
			case COMPARE_OP_LESS:
				return WGPUCompareFunction_Less;
			case COMPARE_OP_EQUAL:
				return WGPUCompareFunction_Equal;
			case COMPARE_OP_LESS_OR_EQUAL:
				return WGPUCompareFunction_LessEqual;
			case COMPARE_OP_GREATER:
				return WGPUCompareFunction_Greater;
			case COMPARE_OP_NOT_EQUAL:
				return WGPUCompareFunction_NotEqual;
			case COMPARE_OP_GREATER_OR_EQUAL:
				return WGPUCompareFunction_GreaterEqual;
			default:
				return WGPUCompareFunction_Always;
		}
	};
	auto map_stencil_op = [](StencilOperation op) -> WGPUStencilOperation {
		switch (op) {
			case STENCIL_OP_KEEP:
				return WGPUStencilOperation_Keep;
			case STENCIL_OP_ZERO:
				return WGPUStencilOperation_Zero;
			case STENCIL_OP_REPLACE:
				return WGPUStencilOperation_Replace;
			case STENCIL_OP_INCREMENT_AND_CLAMP:
				return WGPUStencilOperation_IncrementClamp;
			case STENCIL_OP_DECREMENT_AND_CLAMP:
				return WGPUStencilOperation_DecrementClamp;
			case STENCIL_OP_INVERT:
				return WGPUStencilOperation_Invert;
			case STENCIL_OP_INCREMENT_AND_WRAP:
				return WGPUStencilOperation_IncrementWrap;
			case STENCIL_OP_DECREMENT_AND_WRAP:
				return WGPUStencilOperation_DecrementWrap;
			default:
				return WGPUStencilOperation_Keep;
		}
	};

	// --- Depth/stencil state ---
	WGPUDepthStencilState ds = {};
	WGPUDepthStencilState *ds_ptr = nullptr;
	if ((int32_t)subpass.depth_stencil_reference.attachment != ATTACHMENT_UNUSED) {
		uint32_t ds_att = (uint32_t)subpass.depth_stencil_reference.attachment;
		ds.format = (ds_att < (uint32_t)rp->attachments.size())
				? _data_format_to_wgpu(rp->attachments[ds_att].format)
				: WGPUTextureFormat_Depth24Plus;

		ds.depthWriteEnabled = p_depth_stencil_state.enable_depth_write
				? WGPUOptionalBool_True
				: WGPUOptionalBool_False;
		ds.depthCompare = p_depth_stencil_state.enable_depth_test
				? map_compare(p_depth_stencil_state.depth_compare_operator)
				: WGPUCompareFunction_Always;

		if (p_depth_stencil_state.enable_stencil) {
			ds.stencilFront.compare = map_compare(p_depth_stencil_state.front_op.compare);
			ds.stencilFront.failOp = map_stencil_op(p_depth_stencil_state.front_op.fail);
			ds.stencilFront.depthFailOp = map_stencil_op(p_depth_stencil_state.front_op.depth_fail);
			ds.stencilFront.passOp = map_stencil_op(p_depth_stencil_state.front_op.pass);
			ds.stencilBack.compare = map_compare(p_depth_stencil_state.back_op.compare);
			ds.stencilBack.failOp = map_stencil_op(p_depth_stencil_state.back_op.fail);
			ds.stencilBack.depthFailOp = map_stencil_op(p_depth_stencil_state.back_op.depth_fail);
			ds.stencilBack.passOp = map_stencil_op(p_depth_stencil_state.back_op.pass);
			ds.stencilReadMask = p_depth_stencil_state.front_op.compare_mask;
			ds.stencilWriteMask = p_depth_stencil_state.front_op.write_mask;
			if (p_depth_stencil_state.front_op.compare_mask != p_depth_stencil_state.back_op.compare_mask ||
					p_depth_stencil_state.front_op.write_mask != p_depth_stencil_state.back_op.write_mask) {
				WARN_PRINT_ONCE("WebGPU: Front and back stencil masks differ, but WebGPU only supports a single mask pair. Back-face masks will be ignored (front-face masks used for both).");
			}
		} else {
			ds.stencilFront = { WGPUCompareFunction_Always, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep };
			ds.stencilBack = { WGPUCompareFunction_Always, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep, WGPUStencilOperation_Keep };
			ds.stencilReadMask = 0xFF;
			ds.stencilWriteMask = 0xFF;
		}
		if (p_rasterization_state.depth_bias_enabled) {
			ds.depthBias = (int32_t)p_rasterization_state.depth_bias_constant_factor;
			ds.depthBiasSlopeScale = p_rasterization_state.depth_bias_slope_factor;
			ds.depthBiasClamp = p_rasterization_state.depth_bias_clamp;
		}
		ds_ptr = &ds;
	}

	// --- Blend factor/op helpers ---
	auto blend_factor = [](BlendFactor f) -> WGPUBlendFactor {
		switch (f) {
			case BLEND_FACTOR_ZERO:
				return WGPUBlendFactor_Zero;
			case BLEND_FACTOR_ONE:
				return WGPUBlendFactor_One;
			case BLEND_FACTOR_SRC_COLOR:
				return WGPUBlendFactor_Src;
			case BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
				return WGPUBlendFactor_OneMinusSrc;
			case BLEND_FACTOR_DST_COLOR:
				return WGPUBlendFactor_Dst;
			case BLEND_FACTOR_ONE_MINUS_DST_COLOR:
				return WGPUBlendFactor_OneMinusDst;
			case BLEND_FACTOR_SRC_ALPHA:
				return WGPUBlendFactor_SrcAlpha;
			case BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
				return WGPUBlendFactor_OneMinusSrcAlpha;
			case BLEND_FACTOR_DST_ALPHA:
				return WGPUBlendFactor_DstAlpha;
			case BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
				return WGPUBlendFactor_OneMinusDstAlpha;
			case BLEND_FACTOR_CONSTANT_COLOR:
				return WGPUBlendFactor_Constant;
			case BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
				return WGPUBlendFactor_OneMinusConstant;
			case BLEND_FACTOR_CONSTANT_ALPHA:
				return WGPUBlendFactor_Constant;
			case BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
				return WGPUBlendFactor_OneMinusConstant;
			case BLEND_FACTOR_SRC_ALPHA_SATURATE:
				return WGPUBlendFactor_SrcAlphaSaturated;
			default:
				return WGPUBlendFactor_Zero;
		}
	};
	auto blend_op = [](BlendOperation op) -> WGPUBlendOperation {
		switch (op) {
			case BLEND_OP_ADD:
				return WGPUBlendOperation_Add;
			case BLEND_OP_SUBTRACT:
				return WGPUBlendOperation_Subtract;
			case BLEND_OP_REVERSE_SUBTRACT:
				return WGPUBlendOperation_ReverseSubtract;
			case BLEND_OP_MINIMUM:
				return WGPUBlendOperation_Min;
			case BLEND_OP_MAXIMUM:
				return WGPUBlendOperation_Max;
			default:
				return WGPUBlendOperation_Add;
		}
	};

	// --- Color targets (one per fragment shader @location()) ---
	uint32_t color_target_count = p_color_attachments.size();
	LocalVector<WGPUColorTargetState> color_targets;
	LocalVector<WGPUBlendState> blend_states;
	color_targets.resize(color_target_count);
	blend_states.resize(color_target_count);
	memset(color_targets.ptr(), 0, sizeof(WGPUColorTargetState) * color_target_count);
	memset(blend_states.ptr(), 0, sizeof(WGPUBlendState) * color_target_count);

	for (uint32_t i = 0; i < color_target_count; i++) {
		// Get texture format from subpass color reference i.
		WGPUTextureFormat fmt = WGPUTextureFormat_RGBA8Unorm; // Placeholder for unused slots.
		if (i < (uint32_t)subpass.color_references.size()) {
			int32_t att_idx = subpass.color_references[i].attachment;
			if (att_idx != ATTACHMENT_UNUSED && (uint32_t)att_idx < (uint32_t)rp->attachments.size()) {
				const RDD::Attachment &att_desc = rp->attachments[att_idx];
				fmt = _data_format_to_wgpu(att_desc.format);
				// Mirror the R8/RG8/R16/RG16 → 32-bit promotion in texture_create() for
				// attachments that will also be used as storage textures. Canvas SDF
				// (renderer_canvas_render_rd.cpp) declares R8_UNORM + STORAGE_BIT +
				// COLOR_ATTACHMENT_BIT; the texture gets upgraded to R32Float but the
				// pipeline would otherwise still target R8Unorm → validation mismatch.
				if (att_desc.usage_flags & TEXTURE_USAGE_STORAGE_BIT) {
					fmt = _promote_storage_format(fmt);
				}
				if (fmt == WGPUTextureFormat_Undefined) {
					fmt = WGPUTextureFormat_RGBA8Unorm;
				}
			}
		}
		if (p_color_attachments[i] == ATTACHMENT_UNUSED) {
			// An unused color-target slot must be a genuine "hole" (format
			// Undefined is Dawn's C-API sentinel for the JS API's `null` array
			// entry), not a real format with writes masked off. A pipeline's
			// color-target slots must match a render pass's attachment slots
			// exactly for presence/absence -- Godot's async pipeline
			// precompilation always reserves 3 color-target slots (color,
			// specular-or-unused, velocity-or-unused; see
			// render_forward_clustered.cpp's _get_color_framebuffer_format_for_pipeline())
			// even when a given scene's actual opaque-pass render target only
			// has 1 attachment, so unused slots here are common, not an edge
			// case. Vulkan/Metal/D3D12 don't need this distinction (their
			// UNUSED-attachment references are pass-format-compatible
			// regardless of what the pipeline declares for that slot), but
			// WebGPU's stricter validation rejected a masked-but-formatted
			// target against an actually-absent pass attachment with
			// "Attachment state ... is not compatible". See
			// webgpu_notes/TASKS.md Task 9.5 Round 5.
			color_targets[i].format = WGPUTextureFormat_Undefined;
			color_targets[i].writeMask = WGPUColorWriteMask_None;
			continue;
		}
		color_targets[i].format = fmt;

		// Blend state from p_blend_state.attachments[i].
		if (i < (uint32_t)p_blend_state.attachments.size()) {
			const PipelineColorBlendState::Attachment &ba = p_blend_state.attachments[i];
			WGPUColorWriteMask mask = WGPUColorWriteMask_None;
			if (ba.write_r) {
				mask |= WGPUColorWriteMask_Red;
			}
			if (ba.write_g) {
				mask |= WGPUColorWriteMask_Green;
			}
			if (ba.write_b) {
				mask |= WGPUColorWriteMask_Blue;
			}
			if (ba.write_a) {
				mask |= WGPUColorWriteMask_Alpha;
			}
			// Strip alpha writes for ALL pipelines targeting the swap chain pass.
			// Chrome ignores CompositeAlphaMode_Opaque and composites alpha=0
			// against a gray/white background. Stripping alpha for blended
			// pipelines too ensures the clear value's alpha=1 is never
			// overwritten by shader output.
			// Keyed on rp->is_swap_chain_pass rather than a specific format:
			// since Task 7.11, the swap chain's format is whatever the browser
			// actually prefers (BGRA8Unorm or RGBA8Unorm), not always BGRA8Unorm,
			// so format-value comparison is no longer a reliable "is this the
			// swap chain" signal.
			if (rp->is_swap_chain_pass) {
				mask &= ~WGPUColorWriteMask_Alpha;
				static int _alpha_strip_log = 0;
				if (_alpha_strip_log < 10) {
					[[maybe_unused]] const char *sname = (p_shader.id) ? ((WGShader *)(p_shader.id))->name.utf8().get_data() : "?";
					WEBGPU_DIAG({ console.log('[ALPHA-STRIP] Pipeline #' + $0 + ' fmt=' + $1 + ' mask=' + $2 + ' blend=' + $3 + ' shader=' + UTF8ToString($4)); }, _alpha_strip_log, (int)fmt, (int)mask, ba.enable_blend ? 1 : 0, sname);
					_alpha_strip_log++;
				}
			}
			color_targets[i].writeMask = mask;
			if (ba.enable_blend) {
				// float32-blendable: if the device lacks this feature, blending on
				// float32 render targets causes GPU validation errors. Skip the
				// blend state for these targets — writes still happen (writeMask),
				// but the GPU composites without blending.
				if (!float32_blendable_supported && _is_float32_format(fmt)) {
					static int _f32_blend_skip_log = 0;
					if (_f32_blend_skip_log < 10) {
						[[maybe_unused]] const char *sname = (p_shader.id) ? ((WGShader *)(p_shader.id))->name.utf8().get_data() : "?";
						WEBGPU_DIAG({ console.log('[FLOAT32-BLEND-SKIP] Pipeline fmt=' + $0 + ' shader=' + UTF8ToString($1) + ' — device lacks float32-blendable, disabling blend'); }, (int)fmt, sname);
						_f32_blend_skip_log++;
					}
					// Don't set blend — leave color_targets[i].blend = nullptr.
				} else {
					blend_states[i].color = { blend_op(ba.color_blend_op), blend_factor(ba.src_color_blend_factor), blend_factor(ba.dst_color_blend_factor) };
					blend_states[i].alpha = { blend_op(ba.alpha_blend_op), blend_factor(ba.src_alpha_blend_factor), blend_factor(ba.dst_alpha_blend_factor) };
					color_targets[i].blend = &blend_states[i];
				}
			}
		} else {
			color_targets[i].writeMask = WGPUColorWriteMask_All;
		}
	}

	// --- Fragment state ---
	WGPUFragmentState frag = {};
	frag.module = fragment_module;
	frag.entryPoint = { "main", WGPU_STRLEN };
	frag.targetCount = color_target_count;
	frag.targets = color_targets.ptr();
	if (use_override_path && !frag_constants.is_empty()) {
		frag.constantCount = frag_constants.size();
		frag.constants = frag_constants.ptr();
	}

	// --- Build and create render pipeline ---
	WGPURenderPipelineDescriptor desc = {};
	desc.layout = shader->pipeline_layout;
	desc.vertex = vertex_state;
	desc.primitive = primitive;
	desc.depthStencil = ds_ptr;
	desc.multisample = multisample;
	if (frag.module) {
		desc.fragment = &frag;
	}

	// Label the pipeline with the shader name + a monotonic id so Dawn's
	// validation error messages (and the JS-side create patch) identify it.
	static int _pcreate_id = 0;
	int _pid = _pcreate_id++;
	String _pipeline_label_str = "pipe#" + itos(_pid) + ":" + shader->name;
	CharString _pipeline_label_cs = _pipeline_label_str.utf8();
	desc.label = { _pipeline_label_cs.get_data(), WGPU_STRLEN };

	WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
	if (!pipeline) {
		if (specialized_vertex) {
			wgpuShaderModuleRelease(specialized_vertex);
		}
		if (specialized_fragment) {
			wgpuShaderModuleRelease(specialized_fragment);
		}
		ERR_FAIL_V_MSG(PipelineID(), "WebGPU: Failed to create render pipeline.");
	}

	WGPipelineWrapper *pw = new WGPipelineWrapper();
	pw->type = WGPipelineWrapper::RENDER;
	pw->render_handle = pipeline;
	pw->shader = shader;
	pw->specialized_modules[SHADER_STAGE_VERTEX] = specialized_vertex;
	pw->specialized_modules[SHADER_STAGE_FRAGMENT] = specialized_fragment;
	pw->stencil_reference = p_depth_stencil_state.front_op.reference;
	pw->is_strip = is_strip;
	if (is_strip) {
		// Create a Uint16 variant — WebGPU bakes stripIndexFormat into the pipeline,
		// but Godot only knows the index format at bind time.
		desc.primitive.stripIndexFormat = WGPUIndexFormat_Uint16;
		pw->render_handle_u16 = wgpuDeviceCreateRenderPipeline(device, &desc);
		// Non-fatal if this fails — Uint32 variant still works for the common case.
		if (!pw->render_handle_u16) {
			WARN_PRINT_ONCE("WebGPU: failed to create Uint16 strip pipeline variant.");
		}
	}
	return PipelineID(pw);
}

// =============================================================================
// COMPUTE
// =============================================================================

void RenderingDeviceDriverWebGPU::command_bind_compute_pipeline(CommandBufferID p_cmd_buffer, PipelineID p_pipeline) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_pipeline.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(pw);

	// Ensure we're in a compute pass (end any active render pass).
	if (cmd->active_encoder == WGCommandBuffer::RENDER) {
		cmd->end_active_encoder();
	}
	if (cmd->active_encoder != WGCommandBuffer::COMPUTE) {
		WGPUComputePassDescriptor pass_desc = {};
		cmd->compute_encoder = wgpuCommandEncoderBeginComputePass(cmd->encoder, &pass_desc);
		cmd->active_encoder = WGCommandBuffer::COMPUTE;
		// Fresh pass = fresh synchronization scope; see the field's comment
		// in webgpu_objects.h for why this is tracked at all.
		cmd->render_state.reset_current_compute_pass_textures();
	}

	wgpuComputePassEncoderSetPipeline(cmd->compute_encoder, pw->compute_handle);
	cmd->render_state.current_pipeline = pw;

	// Pre-bind empty bind groups at pipeline layout gap slots.
	if (pw->shader) {
		for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
			wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, gap_idx, empty_bind_group, 0, nullptr);
		}
	}
}

void RenderingDeviceDriverWebGPU::command_bind_compute_uniform_sets(CommandBufferID p_cmd_buffer, VectorView<UniformSetID> p_uniform_sets, ShaderID p_shader, uint32_t p_first_set_index, uint32_t p_set_count, uint32_t p_dynamic_offsets) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->compute_encoder);

	WGShader *pipeline_shader = cmd->render_state.current_pipeline ? cmd->render_state.current_pipeline->shader : nullptr;

	// Task 7.5: mirror the render path's dynamic offset unpacking.
	static constexpr uint32_t MAX_DYNAMIC_BUFFERS = 8;
	uint32_t dyn_shift = 0;

	// A uniform set's read_write-storage-texture shadow companions (see
	// WGUniformSet::RWShadowRegistration) are only ever refreshed by
	// texture_update() -- a CPU upload. A source texture that's instead
	// written by other GPU compute/render passes (e.g. bokeh_dof.glsl's
	// `color_image`, never CPU-uploaded) leaves its shadow stuck at its
	// initial zeroed contents forever, silently corrupting every read
	// through it (observed as the whole DOF-processed frame going solid
	// black, with zero validation errors). Refresh via a real GPU-side copy
	// immediately before binding, every time -- copy commands can't be
	// recorded inside an active compute pass, so this ends the pass, copies
	// on the raw encoder, then restarts and rebinds, mirroring the existing
	// mid-pass restart machinery in _flush_push_constants()'s ring-overflow
	// path (below, minus that path's flush/submit, since no push-constant
	// data needs freezing here).
	auto refresh_rw_shadows = [&](WGUniformSet *us) {
		if (us->rw_shadow_registrations.is_empty() || !cmd->compute_encoder) {
			return;
		}
		wgpuComputePassEncoderEnd(cmd->compute_encoder);
		wgpuComputePassEncoderRelease(cmd->compute_encoder);
		cmd->compute_encoder = nullptr;

		for (const WGUniformSet::RWShadowRegistration &reg : us->rw_shadow_registrations) {
			WGPUTexelCopyTextureInfo src_copy = {};
			src_copy.texture = reg.source;
			src_copy.aspect = WGPUTextureAspect_All;
			WGPUTexelCopyTextureInfo dst_copy = {};
			dst_copy.texture = reg.shadow;
			dst_copy.aspect = WGPUTextureAspect_All;
			WGPUExtent3D extent = { reg.width, reg.height, reg.depth };
			wgpuCommandEncoderCopyTextureToTexture(cmd->encoder, &src_copy, &dst_copy, &extent);
		}

		WGPUComputePassDescriptor cp_desc = {};
		cmd->compute_encoder = wgpuCommandEncoderBeginComputePass(cmd->encoder, &cp_desc);

		WGPipelineWrapper *pw = cmd->render_state.current_pipeline;
		if (pw) {
			wgpuComputePassEncoderSetPipeline(cmd->compute_encoder, pw->compute_handle);
			if (pw->shader) {
				for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
					wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, gap_idx, empty_bind_group, 0, nullptr);
				}
			}
		}
		for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
			const auto &bs = cmd->last_bound_state[i];
			if (bs.group) {
				wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, i, bs.group, bs.dynamic_offset_count, bs.dynamic_offsets);
			}
		}
		for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
			cmd->bound_bind_groups[i] = nullptr;
		}
	};

	// Detects and resolves the compute-list analog of command_begin_render_pass()'s
	// "proactive encoder isolation": a texture bound as a storage image in one
	// dispatch of the current compute list and then bound again (sampled) in a
	// later dispatch of the SAME still-open WGPUComputePassEncoder puts both
	// usages in one synchronization scope, which Dawn rejects outright ("includes
	// writable usage and another usage in the same synchronization scope"),
	// invalidating the whole command buffer -- e.g. FSR1/FSR2's EASU-write-then-
	// RCAS-sample ping-pong on their shared upscale texture. Unlike the render-pass
	// case, no eager submit is needed: ending and beginning a fresh pass on the
	// same command encoder starts a new synchronization scope on its own, which
	// prevents the conflict rather than merely limiting its blast radius.
	auto split_compute_pass_if_conflicting = [&](WGUniformSet *us) {
		if (!cmd->compute_encoder) {
			return;
		}
		bool conflict = false;
		for (const KeyValue<uint32_t, WGTexture *> &kv : us->bound_textures) {
			WGTexture *tex = kv.value;
			bool dual_capable = tex && (tex->usage & WGPUTextureUsage_StorageBinding) && (tex->usage & WGPUTextureUsage_TextureBinding);
			if (dual_capable && cmd->render_state.has_current_compute_pass_texture(tex->gpu_handle())) {
				conflict = true;
				break;
			}
		}
		if (conflict) {
			wgpuComputePassEncoderEnd(cmd->compute_encoder);
			wgpuComputePassEncoderRelease(cmd->compute_encoder);
			cmd->compute_encoder = nullptr;

			WGPUComputePassDescriptor cp_desc = {};
			cmd->compute_encoder = wgpuCommandEncoderBeginComputePass(cmd->encoder, &cp_desc);
			cmd->render_state.reset_current_compute_pass_textures();

			WGPipelineWrapper *pw = cmd->render_state.current_pipeline;
			if (pw) {
				wgpuComputePassEncoderSetPipeline(cmd->compute_encoder, pw->compute_handle);
				if (pw->shader) {
					for (uint32_t gap_idx : pw->shader->gap_bind_group_indices) {
						wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, gap_idx, empty_bind_group, 0, nullptr);
					}
				}
			}
			for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
				const auto &bs = cmd->last_bound_state[i];
				if (bs.group) {
					wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, i, bs.group, bs.dynamic_offset_count, bs.dynamic_offsets);
				}
			}
			for (uint32_t i = 0; i < WGCommandBuffer::MAX_BIND_GROUPS; i++) {
				cmd->bound_bind_groups[i] = nullptr;
			}
		}

		// Record this set's dual-capable textures against the (possibly
		// just-reset) current pass so a later conflicting reuse is caught too.
		for (const KeyValue<uint32_t, WGTexture *> &kv : us->bound_textures) {
			WGTexture *tex = kv.value;
			if (tex && (tex->usage & WGPUTextureUsage_StorageBinding) && (tex->usage & WGPUTextureUsage_TextureBinding)) {
				cmd->render_state.add_current_compute_pass_texture(tex->gpu_handle());
			}
		}
	};

	for (uint32_t i = 0; i < p_set_count; i++) {
		WGUniformSet *us = (WGUniformSet *)(p_uniform_sets[i].id);
		if (us && us->handle) {
			split_compute_pass_if_conflicting(us);
			refresh_rw_shadows(us);
			uint32_t set_idx = p_first_set_index + i;
			WGPUBindGroup bg_to_bind = _get_compatible_bind_group(us, pipeline_shader, set_idx);

			uint32_t set_dyn_offsets[MAX_DYNAMIC_BUFFERS] = {};
			uint32_t num_dyn = us->dynamic_buffers.size();
			if (num_dyn > MAX_DYNAMIC_BUFFERS) {
				num_dyn = MAX_DYNAMIC_BUFFERS;
			}
			for (uint32_t j = 0; j < num_dyn; j++) {
				uint32_t frame_idx = (p_dynamic_offsets >> dyn_shift) & 0xFu;
				dyn_shift += 4u;
				const WGBuffer *dbuf = us->dynamic_buffers[j];
				set_dyn_offsets[j] = (uint32_t)(frame_idx * (dbuf ? dbuf->per_frame_size : 0));
			}

			const bool is_pc_merged = (pipeline_shader && pipeline_shader->push_constant_size > 0 &&
					set_idx == pipeline_shader->push_constant_bind_group &&
					pipeline_shader->merged_pc_group_layout != nullptr);

			if (is_pc_merged) {
				// Save material offsets; _flush_push_constants preserves them each dispatch.
				cmd->pc_group_material_dyn_count = num_dyn;
				for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_PC_GROUP_MATERIAL_DYN; j++) {
					cmd->pc_group_material_dyn_offsets[j] = set_dyn_offsets[j];
				}
				if (num_dyn < MAX_DYNAMIC_BUFFERS) {
					set_dyn_offsets[num_dyn] = cmd->last_flushed_pc_offset;
					num_dyn++;
				}
				cmd->current_pc_bind_group = bg_to_bind;
				wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, set_idx, bg_to_bind, num_dyn, set_dyn_offsets);
			} else if (pipeline_shader && pipeline_shader->push_constant_size > 0 &&
					set_idx == pipeline_shader->push_constant_bind_group) {
				cmd->current_pc_bind_group = nullptr;
				cmd->pc_group_material_dyn_count = 0;
				wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, set_idx, bg_to_bind, num_dyn, num_dyn ? set_dyn_offsets : nullptr);
			} else {
				wgpuComputePassEncoderSetBindGroup(cmd->compute_encoder, set_idx, bg_to_bind, num_dyn, num_dyn ? set_dyn_offsets : nullptr);
			}
			// Save full state for mid-pass restart on ring overflow.
			if (set_idx < WGCommandBuffer::MAX_BIND_GROUPS) {
				auto &bs = cmd->last_bound_state[set_idx];
				bs.group = bg_to_bind;
				bs.dynamic_offset_count = num_dyn;
				for (uint32_t j = 0; j < num_dyn && j < WGCommandBuffer::MAX_BIND_GROUP_DYN_OFFSETS; j++) {
					bs.dynamic_offsets[j] = set_dyn_offsets[j];
				}
			}
		}
	}
}

void RenderingDeviceDriverWebGPU::command_compute_dispatch(CommandBufferID p_cmd_buffer, uint32_t p_x_groups, uint32_t p_y_groups, uint32_t p_z_groups) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_COND(!cmd->compute_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuComputePassEncoderDispatchWorkgroups(cmd->compute_encoder, p_x_groups, p_y_groups, p_z_groups);
}

void RenderingDeviceDriverWebGPU::command_compute_dispatch_indirect(CommandBufferID p_cmd_buffer, BufferID p_indirect_buffer, uint64_t p_offset) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	WGBuffer *indirect = (WGBuffer *)(p_indirect_buffer.id);
	ERR_FAIL_NULL(cmd);
	ERR_FAIL_NULL(indirect);
	ERR_FAIL_COND(!cmd->compute_encoder);

	if (cmd->render_state.current_pipeline) {
		_flush_push_constants(cmd, cmd->render_state.current_pipeline->shader);
	}

	wgpuComputePassEncoderDispatchWorkgroupsIndirect(cmd->compute_encoder, indirect->handle, p_offset);
}

RDD::PipelineID RenderingDeviceDriverWebGPU::compute_pipeline_create(ShaderID p_shader, VectorView<PipelineSpecializationConstant> p_specialization_constants) {
	WGShader *shader = (WGShader *)(p_shader.id);
	ERR_FAIL_COND_V(!shader, PipelineID());
	ERR_FAIL_COND_V_MSG(!shader->stage_modules[SHADER_STAGE_COMPUTE], PipelineID(),
			"WebGPU: compute_pipeline_create called with a shader that has no compute stage.");

	// Specialization constants: override path or legacy module-patching path.
	WGPUShaderModule compute_module = shader->stage_modules[SHADER_STAGE_COMPUTE];
	WGPUShaderModule specialized_compute = nullptr;
	LocalVector<WGPUConstantEntry> compute_constants;
	LocalVector<CharString> compute_key_storage;
	bool use_override_path = shader->has_override_declarations;
	if (p_specialization_constants.size() > 0) {
		if (use_override_path) {
			const HashSet<uint32_t> &cmp_ids = shader->stage_override_ids[SHADER_STAGE_COMPUTE];
			for (uint32_t i = 0; i < p_specialization_constants.size(); i++) {
				const PipelineSpecializationConstant &sc = p_specialization_constants[i];
				if (!cmp_ids.has(sc.constant_id)) {
					continue; // This constant ID doesn't exist in the compute stage's WGSL.
				}
				WGPUConstantEntry entry = {};
				String key_str = itos(sc.constant_id);
				CharString key_cs = key_str.utf8();
				compute_key_storage.push_back(key_cs);
				entry.key = { compute_key_storage[compute_key_storage.size() - 1].get_data(), WGPU_STRLEN };
				switch (sc.type) {
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
						entry.value = sc.bool_value ? 1.0 : 0.0;
						break;
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
						entry.value = (double)sc.int_value;
						break;
					case PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
						entry.value = (double)sc.float_value;
						break;
				}
				compute_constants.push_back(entry);
			}
		} else if (!shader->stage_spirv[SHADER_STAGE_COMPUTE].is_empty()) {
			specialized_compute = _create_module_with_spec_constants(
					shader->stage_spirv[SHADER_STAGE_COMPUTE], p_specialization_constants, SHADER_STAGE_COMPUTE, shader->rw_storage_splits);
			if (specialized_compute) {
				compute_module = specialized_compute;
			}
		}
	}

	WGPUComputePipelineDescriptor desc = {};
	desc.layout = shader->pipeline_layout;
	desc.compute.module = compute_module;
	desc.compute.entryPoint = { "main", WGPU_STRLEN };
	if (use_override_path && !compute_constants.is_empty()) {
		desc.compute.constantCount = compute_constants.size();
		desc.compute.constants = compute_constants.ptr();
	}

	WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &desc);
	if (!pipeline) {
		if (specialized_compute) {
			wgpuShaderModuleRelease(specialized_compute);
		}
		ERR_FAIL_V_MSG(PipelineID(), "WebGPU: Failed to create compute pipeline.");
	}

	WGPipelineWrapper *pw = new WGPipelineWrapper();
	pw->type = WGPipelineWrapper::COMPUTE;
	pw->compute_handle = pipeline;
	pw->shader = shader;
	pw->specialized_modules[SHADER_STAGE_COMPUTE] = specialized_compute;
	return PipelineID(pw);
}

// =============================================================================
// QUERIES
// =============================================================================

RDD::QueryPoolID RenderingDeviceDriverWebGPU::timestamp_query_pool_create(uint32_t p_query_count) {
	WGQueryPool *pool = new WGQueryPool();
	pool->count = p_query_count;
	pool->cpu_results = (uint64_t *)memalloc(sizeof(uint64_t) * p_query_count);
	memset(pool->cpu_results, 0, sizeof(uint64_t) * p_query_count);

	if (!timestamp_supported) {
		// No hardware timestamp support — return a dummy pool that returns zeros.
		pool->is_real = false;
		return QueryPoolID(pool);
	}

	// Create query set.
	WGPUQuerySetDescriptor qs_desc = {};
	qs_desc.type = WGPUQueryType_Timestamp;
	qs_desc.count = p_query_count;
	pool->handle = wgpuDeviceCreateQuerySet(device, &qs_desc);
	if (!pool->handle) {
		WARN_PRINT("WebGPU: Failed to create timestamp query set, falling back to dummy timestamps.");
		pool->is_real = false;
		return QueryPoolID(pool);
	}

	// Create resolve buffer (GPU-side, receives resolved query results).
	uint64_t buf_size = sizeof(uint64_t) * p_query_count;
	{
		WGPUBufferDescriptor bd = {};
		bd.size = buf_size;
		bd.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
		pool->resolve_buffer = wgpuDeviceCreateBuffer(device, &bd);
	}

	// Create readback buffer (CPU-readable staging buffer).
	{
		WGPUBufferDescriptor bd = {};
		bd.size = buf_size;
		bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
		pool->readback_buffer = wgpuDeviceCreateBuffer(device, &bd);
	}

	if (!pool->resolve_buffer || !pool->readback_buffer) {
		WARN_PRINT("WebGPU: Failed to create timestamp resolve/readback buffers.");
		if (pool->handle) {
			wgpuQuerySetRelease(pool->handle);
			pool->handle = nullptr;
		}
		if (pool->resolve_buffer) {
			wgpuBufferRelease(pool->resolve_buffer);
			pool->resolve_buffer = nullptr;
		}
		if (pool->readback_buffer) {
			wgpuBufferRelease(pool->readback_buffer);
			pool->readback_buffer = nullptr;
		}
		pool->is_real = false;
		return QueryPoolID(pool);
	}

	pool->is_real = true;
	return QueryPoolID(pool);
}

void RenderingDeviceDriverWebGPU::timestamp_query_pool_free(QueryPoolID p_pool_id) {
	WGQueryPool *pool = (WGQueryPool *)(p_pool_id.id);
	if (!pool) {
		return;
	}

	// If an async readback callback is in flight, mark freed and let
	// the callback handle cleanup (use-after-free prevention).
	if (pool->readback_pending) {
		pool->freed = true;
		return;
	}

	if (pool->handle) {
		wgpuQuerySetRelease(pool->handle);
	}
	if (pool->resolve_buffer) {
		wgpuBufferRelease(pool->resolve_buffer);
	}
	if (pool->readback_buffer) {
		wgpuBufferRelease(pool->readback_buffer);
	}
	if (pool->cpu_results) {
		memfree(pool->cpu_results);
	}
	delete pool;
}

void RenderingDeviceDriverWebGPU::timestamp_query_pool_get_results(QueryPoolID p_pool_id, uint32_t p_query_count, uint64_t *r_results) {
	WGQueryPool *pool = (WGQueryPool *)(p_pool_id.id);
	if (!pool || !pool->is_real) {
		memset(r_results, 0, sizeof(uint64_t) * p_query_count);
		return;
	}

	// Copy from the CPU shadow buffer (populated by the async readback callback).
	uint32_t copy_count = MIN(p_query_count, pool->count);
	memcpy(r_results, pool->cpu_results, sizeof(uint64_t) * copy_count);
	if (p_query_count > copy_count) {
		memset(r_results + copy_count, 0, sizeof(uint64_t) * (p_query_count - copy_count));
	}
}

uint64_t RenderingDeviceDriverWebGPU::timestamp_query_result_to_time(uint64_t p_result) {
	return p_result; // WebGPU timestamps are already in nanoseconds.
}

void RenderingDeviceDriverWebGPU::command_timestamp_query_pool_reset(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_query_count) {
	// No-op: WebGPU query sets don't need reset. Resolve happens in command_buffer_end().
}

// Forward declared at the top of the file; defined here.
static void _timestamp_readback_callback(WGPUMapAsyncStatus p_status, WGPUStringView p_message, void *p_userdata1, void *p_userdata2) {
	WGQueryPool *pool = (WGQueryPool *)p_userdata1;
	if (!pool) {
		return;
	}

	// Pool was freed while this async readback was in flight — clean up.
	if (pool->freed) {
		if (pool->readback_buffer) {
			if (p_status == WGPUMapAsyncStatus_Success) {
				wgpuBufferUnmap(pool->readback_buffer);
			}
			wgpuBufferRelease(pool->readback_buffer);
		}
		if (pool->resolve_buffer) {
			wgpuBufferRelease(pool->resolve_buffer);
		}
		if (pool->handle) {
			wgpuQuerySetRelease(pool->handle);
		}
		if (pool->cpu_results) {
			memfree(pool->cpu_results);
		}
		delete pool;
		return;
	}

	// Stale callback from a cancelled/superseded mapAsync — ignore.
	uint32_t cb_generation = (uint32_t)(uintptr_t)p_userdata2;
	if (cb_generation != pool->map_generation) {
		if (p_status == WGPUMapAsyncStatus_Success) {
			wgpuBufferUnmap(pool->readback_buffer);
		}
		return;
	}

	if (p_status == WGPUMapAsyncStatus_Success) {
		const void *data = wgpuBufferGetConstMappedRange(pool->readback_buffer, 0, sizeof(uint64_t) * pool->count);
		if (data) {
			memcpy(pool->cpu_results, data, sizeof(uint64_t) * pool->count);
		}
		wgpuBufferUnmap(pool->readback_buffer);
		pool->readback_pending = false;
	}
	// On failure/abort: do NOT clear readback_pending.
	// command_buffer_end() handles cleanup and re-issue.
}

void RenderingDeviceDriverWebGPU::command_timestamp_write(CommandBufferID p_cmd_buffer, QueryPoolID p_pool_id, uint32_t p_index) {
	WGQueryPool *pool = (WGQueryPool *)(p_pool_id.id);
	if (!pool || !pool->is_real || p_index >= pool->count) {
		return;
	}

	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	// wgpuCommandEncoderWriteTimestamp requires no active render/compute pass.
	cmd->end_active_encoder();

	wgpuCommandEncoderWriteTimestamp(cmd->encoder, pool->handle, p_index);

	// Track this pool so we resolve it in command_buffer_end.
	bool already_tracked = false;
	for (uint32_t i = 0; i < cmd->written_query_pools.size(); i++) {
		if (cmd->written_query_pools[i] == pool) {
			already_tracked = true;
			break;
		}
	}
	if (!already_tracked) {
		cmd->written_query_pools.push_back(pool);
	}
}

// =============================================================================
// LABELS & DEBUG
// =============================================================================

void RenderingDeviceDriverWebGPU::command_begin_label(CommandBufferID p_cmd_buffer, const char *p_label_name, const Color &p_color) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderPushDebugGroup(cmd->render_encoder, WGPUStringView{ p_label_name, WGPU_STRLEN });
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderPushDebugGroup(cmd->compute_encoder, WGPUStringView{ p_label_name, WGPU_STRLEN });
	} else if (cmd->encoder) {
		wgpuCommandEncoderPushDebugGroup(cmd->encoder, WGPUStringView{ p_label_name, WGPU_STRLEN });
	}
}

void RenderingDeviceDriverWebGPU::command_end_label(CommandBufferID p_cmd_buffer) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderPopDebugGroup(cmd->render_encoder);
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderPopDebugGroup(cmd->compute_encoder);
	} else if (cmd->encoder) {
		wgpuCommandEncoderPopDebugGroup(cmd->encoder);
	}
}

void RenderingDeviceDriverWebGPU::command_insert_breadcrumb(CommandBufferID p_cmd_buffer, uint32_t p_data) {
	WGCommandBuffer *cmd = (WGCommandBuffer *)(p_cmd_buffer.id);
	ERR_FAIL_NULL(cmd);

	char marker[32];
	snprintf(marker, sizeof(marker), "breadcrumb_%u", p_data);

	if (cmd->render_encoder) {
		wgpuRenderPassEncoderInsertDebugMarker(cmd->render_encoder, WGPUStringView{ marker, WGPU_STRLEN });
	} else if (cmd->compute_encoder) {
		wgpuComputePassEncoderInsertDebugMarker(cmd->compute_encoder, WGPUStringView{ marker, WGPU_STRLEN });
	} else if (cmd->encoder) {
		wgpuCommandEncoderInsertDebugMarker(cmd->encoder, WGPUStringView{ marker, WGPU_STRLEN });
	}
}

// =============================================================================
// SUBMISSION
// =============================================================================

void RenderingDeviceDriverWebGPU::begin_segment(uint32_t p_frame_index, uint32_t p_frames_drawn) {
	frame_index = p_frame_index;
	frames_drawn = p_frames_drawn;

	// Performance counter tracking — gated behind WEBGPU_VERBOSE (see the
	// #define near the top of this file). This used to be always-on with the
	// reasoning that a 1 log/sec console.log is negligible overhead, but it
	// spams the browser console in production builds (see plan-of-attack.md
	// Tier 1 #4) — anyone profiling should opt in explicitly instead.
#ifdef WEBGPU_VERBOSE
	perf.frames_since_log++;
	double now = EM_ASM_DOUBLE({ return performance.now(); });
	if (perf.last_log_time == 0) {
		perf.last_log_time = now;
	} else if (now - perf.last_log_time >= 1000.0) {
		double elapsed = (now - perf.last_log_time) / 1000.0;
		uint32_t fps = (uint32_t)(perf.frames_since_log / elapsed);
		uint32_t f = perf.frames_since_log > 0 ? perf.frames_since_log : 1;
		EM_ASM({ console.log('[PERF] fps=' + $0 +
						 ' draws/f=' + $1 +
						 ' SetBG/f=' + $2 +
						 ' PC/f=' + $3 +
						 ' RP/f=' + $4 +
						 ' SetVB/f=' + $5 +
						 ' FI/f=' + $6 +
						 ' RingOF/f=' + $7); }, fps, perf.draw_calls / f, perf.set_bind_group_calls / f, perf.push_constant_writes / f, perf.render_passes / f, perf.set_vertex_buffer_calls / f, perf.first_instance_draws / f, perf.ring_overflows / f);
		perf.reset();
		perf.frames_since_log = 0;
		perf.last_log_time = now;
	}
#endif

	// Reset push constant ring buffer offset and shadow buffer tracking at the start of each segment.
	push_constant_ring_offset = 0;
	push_constant_shadow_dirty_start = UINT32_MAX;
	push_constant_shadow_dirty_end = 0;
}

void RenderingDeviceDriverWebGPU::end_segment() {
	// Nothing to do.
}

// =============================================================================
// MISC
// =============================================================================

void RenderingDeviceDriverWebGPU::set_object_name(ObjectType p_type, ID p_driver_id, const String &p_name) {
	// Propagate names to Dawn so validation errors reference the Godot resource
	// (otherwise Dawn reports "[Texture (unlabeled ...)]"). CharString must live
	// until wgpuXxxSetLabel returns — Dawn copies the string internally.
	if (p_driver_id.id == 0) {
		return;
	}
	const CharString name_utf8 = p_name.utf8();
	const WGPUStringView label = { name_utf8.get_data(), WGPU_STRLEN };
	switch (p_type) {
		case OBJECT_TYPE_TEXTURE: {
			WGTexture *tex = (WGTexture *)(p_driver_id.id);
			if (tex && tex->handle) {
				wgpuTextureSetLabel(tex->handle, label);
			}
		} break;
		case OBJECT_TYPE_SAMPLER: {
			// SamplerID stores the raw WGPUSampler handle directly.
			WGPUSampler sampler = (WGPUSampler)(p_driver_id.id);
			wgpuSamplerSetLabel(sampler, label);
		} break;
		case OBJECT_TYPE_BUFFER: {
			WGBuffer *buf = (WGBuffer *)(p_driver_id.id);
			if (buf && buf->handle) {
				wgpuBufferSetLabel(buf->handle, label);
			}
		} break;
		case OBJECT_TYPE_SHADER: {
			WGShader *shader = (WGShader *)(p_driver_id.id);
			if (shader) {
				shader->name = p_name;
				for (uint32_t i = 0; i < 6; i++) {
					if (shader->stage_modules[i]) {
						wgpuShaderModuleSetLabel(shader->stage_modules[i], label);
					}
				}
			}
		} break;
		case OBJECT_TYPE_UNIFORM_SET: {
			WGUniformSet *us = (WGUniformSet *)(p_driver_id.id);
			if (us && us->handle) {
				wgpuBindGroupSetLabel(us->handle, label);
			}
		} break;
		case OBJECT_TYPE_PIPELINE: {
			WGPipelineWrapper *pw = (WGPipelineWrapper *)(p_driver_id.id);
			if (!pw) {
				break;
			}
			if (pw->type == WGPipelineWrapper::RENDER && pw->render_handle) {
				wgpuRenderPipelineSetLabel(pw->render_handle, label);
			} else if (pw->type == WGPipelineWrapper::COMPUTE && pw->compute_handle) {
				wgpuComputePipelineSetLabel(pw->compute_handle, label);
			}
		} break;
		case OBJECT_TYPE_ACCELERATION_STRUCTURE:
		case OBJECT_TYPE_RAYTRACING_PIPELINE: {
			// Not supported by WebGPU.
		} break;
	}
}

uint64_t RenderingDeviceDriverWebGPU::get_resource_native_handle(DriverResource p_type, ID p_driver_id) {
	return p_driver_id.id;
}

uint64_t RenderingDeviceDriverWebGPU::get_total_memory_used() {
	return 0; // TODO: Track internally.
}

uint64_t RenderingDeviceDriverWebGPU::get_lazily_memory_used() {
	return 0;
}

uint64_t RenderingDeviceDriverWebGPU::limit_get(Limit p_limit) {
	switch (p_limit) {
		case LIMIT_MAX_BOUND_UNIFORM_SETS:
			return device_limits.maxBindGroups;
		case LIMIT_MAX_FRAMEBUFFER_COLOR_ATTACHMENTS:
			return device_limits.maxColorAttachments;
		case LIMIT_MAX_TEXTURES_PER_UNIFORM_SET:
			return device_limits.maxSampledTexturesPerShaderStage;
		case LIMIT_MAX_SAMPLERS_PER_UNIFORM_SET:
			return device_limits.maxSamplersPerShaderStage;
		case LIMIT_MAX_STORAGE_BUFFERS_PER_UNIFORM_SET:
			return device_limits.maxStorageBuffersPerShaderStage;
		case LIMIT_MAX_STORAGE_IMAGES_PER_UNIFORM_SET:
			return device_limits.maxStorageTexturesPerShaderStage;
		case LIMIT_MAX_UNIFORM_BUFFERS_PER_UNIFORM_SET:
			return device_limits.maxUniformBuffersPerShaderStage;
		case LIMIT_MAX_PUSH_CONSTANT_SIZE:
			return 128; // Emulated via ring buffer.
		case LIMIT_MAX_UNIFORM_BUFFER_SIZE:
			return device_limits.maxUniformBufferBindingSize;
		case LIMIT_MAX_TEXTURES_PER_SHADER_STAGE:
			return device_limits.maxSampledTexturesPerShaderStage;
		case LIMIT_MAX_TEXTURE_ARRAY_LAYERS:
			return device_limits.maxTextureArrayLayers;
		case LIMIT_MAX_TEXTURE_SIZE_1D:
			return device_limits.maxTextureDimension1D;
		case LIMIT_MAX_TEXTURE_SIZE_2D:
			return device_limits.maxTextureDimension2D;
		case LIMIT_MAX_TEXTURE_SIZE_3D:
			return device_limits.maxTextureDimension3D;
		case LIMIT_MAX_TEXTURE_SIZE_CUBE:
			return device_limits.maxTextureDimension2D; // Cube faces are 2D.
		case LIMIT_MAX_VERTEX_INPUT_ATTRIBUTE_OFFSET:
			return device_limits.maxVertexBufferArrayStride;
		case LIMIT_MAX_VERTEX_INPUT_ATTRIBUTES:
			return device_limits.maxVertexAttributes;
		case LIMIT_MAX_VERTEX_INPUT_BINDINGS:
			return device_limits.maxVertexBuffers;
		case LIMIT_MAX_COMPUTE_SHARED_MEMORY_SIZE:
			return device_limits.maxComputeWorkgroupStorageSize;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_X:
			return device_limits.maxComputeWorkgroupsPerDimension;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Y:
			return device_limits.maxComputeWorkgroupsPerDimension;
		case LIMIT_MAX_COMPUTE_WORKGROUP_COUNT_Z:
			return device_limits.maxComputeWorkgroupsPerDimension;
		case LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS:
			return device_limits.maxComputeInvocationsPerWorkgroup;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_X:
			return device_limits.maxComputeWorkgroupSizeX;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Y:
			return device_limits.maxComputeWorkgroupSizeY;
		case LIMIT_MAX_COMPUTE_WORKGROUP_SIZE_Z:
			return device_limits.maxComputeWorkgroupSizeZ;
		case LIMIT_MAX_SHADER_VARYINGS:
			return device_limits.maxInterStageShaderVariables;
		case LIMIT_SUBGROUP_SIZE:
			return 0; // Subgroups not available in WebGPU.
		case LIMIT_SUBGROUP_MIN_SIZE:
			return 0;
		case LIMIT_SUBGROUP_MAX_SIZE:
			return 0;
		case LIMIT_SUBGROUP_IN_SHADERS:
			return 0;
		case LIMIT_SUBGROUP_OPERATIONS:
			return 0;
		default:
			return 0;
	}
}

uint64_t RenderingDeviceDriverWebGPU::api_trait_get(ApiTrait p_trait) {
	switch (p_trait) {
		case API_TRAIT_HONORS_PIPELINE_BARRIERS:
			return 0; // No barriers in WebGPU.
		case API_TRAIT_SHADER_CHANGE_INVALIDATION:
			return SHADER_CHANGE_INVALIDATION_ALL_BOUND_UNIFORM_SETS;
		case API_TRAIT_TEXTURE_TRANSFER_ALIGNMENT:
			return 256;
		case API_TRAIT_TEXTURE_DATA_ROW_PITCH_STEP:
			return 256;
		case API_TRAIT_SECONDARY_VIEWPORT_SCISSOR:
			return 0;
		case API_TRAIT_CLEARS_WITH_COPY_ENGINE:
			return 0;
		case API_TRAIT_USE_GENERAL_IN_COPY_QUEUES:
			return 0;
		case API_TRAIT_BUFFERS_REQUIRE_TRANSITIONS:
			return 0;
		case API_TRAIT_TEXTURE_OUTPUTS_REQUIRE_CLEARS:
			return 0;
		// Force RD::texture_get_data() to route through driver->texture_get_data()
		// because the synchronous draw-graph + buffer_map path can't wait for
		// wgpuBufferMapAsync (no Asyncify, can't yield to JS during a sync C call).
		// driver->texture_get_data() handles this with a persistent staging buffer
		// + frame-deferred async map (see ASYNC BUFFER READBACK section).
		case API_TRAIT_TEXTURE_GET_DATA_VIA_DRIVER:
			return 1;
		// Skip transfer-worker path for layered Texture2DArray uploads; we
		// implement texture_initialize_direct_layered using wgpuQueueWriteTexture
		// directly (no GPU staging buffer, no command encoder, no barriers).
		// Eliminates a same-size wasted VRAM allocation per Texture2DArray
		// upload and the queue serialization that came with it.
		case API_TRAIT_TEXTURE_INITIALIZE_DIRECT_WRITE:
			return 1;
		// Use mappedAtCreation for buffer creation with initial data —
		// bypasses staging buffer + wgpuQueueWriteBuffer overhead entirely.
		case API_TRAIT_BUFFER_CREATE_MAPPED_AT_CREATION:
			return 1;
		// Cap the staging buffer pool to 16 MB. WebGPU staging blocks use
		// CPU shadow memory (memalloc), so idle blocks waste heap after
		// the loading spike. 16 MB is generous for per-frame dynamic
		// updates; overflow stalls briefly and reuses blocks.
		case API_TRAIT_STAGING_BUFFER_MAX_SIZE_MB:
			return 16;
		case API_TRAIT_SKELETON_BUFFER_DIRECT_WRITE:
			return 1;
		// Force dual-paraboloid shadows for omni lights. Cubemap shadows
		// require 6 render pass encoder cycles + 2 copy-to-atlas ops per
		// light; dual-paraboloid uses 2 passes directly into the atlas,
		// saving ~80% of shadow encoder overhead for typical scenes.
		case API_TRAIT_FORCE_OMNI_DUAL_PARABOLOID:
			return 1;
		// Batch consecutive same-mesh shadow draws into instanced draws.
		// Reduces per-draw IPC from 2 crossings/draw to 2 crossings/batch.
		case API_TRAIT_BATCH_INSTANCE_DRAWS:
			return 1;
		// Pass instance index via firstInstance instead of push constants.
		// Eliminates per-draw SetBindGroup for push constant ring buffer.
		case API_TRAIT_FIRST_INSTANCE_INDEX:
			return 1;
		// buffer_map() returns a CPU shadow copy; RenderingDevice::_end_frame() must
		// unmap every upload staging block every frame to flush it via wgpuQueueWriteBuffer.
		case API_TRAIT_BUFFER_MAP_RETURNS_SHADOW_COPY:
			return 1;
		default:
			return RenderingDeviceDriver::api_trait_get(p_trait);
	}
}

bool RenderingDeviceDriverWebGPU::has_feature(Features p_feature) {
	switch (p_feature) {
		case SUPPORTS_HALF_FLOAT:
			return false; // WebGPU shader-f16 extension not reliably available; avoid f16 in generated SPIR-V.
		case SUPPORTS_FRAGMENT_SHADER_WITH_ONLY_SIDE_EFFECTS:
			// This was wrongly true: a WebGPU render *pass* can have zero color
			// attachments as long as a depth-stencil attachment is present, but
			// "fragment shader with only side effects" means a pipeline/pass with
			// NO attachments at all (color or depth) -- WebGPU always rejects that
			// outright ("No attachment was specified" at CreateRenderPipeline,
			// "Render pass has no attachments" at BeginRenderPass), unlike Vulkan/
			// Metal/D3D12 which do support it. cluster_builder_rd.cpp already has a
			// complete, working fallback for platforms lacking this (a real color
			// attachment via SHADER_USE_ATTACHMENT), gated on exactly this flag --
			// it just never took that path here. Found via a real Chrome run
			// against ClusterRenderShaderRD. See webgpu_notes/TASKS.md Task 9.5
			// Round 12.
			return false;
		case SUPPORTS_IMAGE_ATOMIC_32_BIT:
			return false; // WebGPU has no image atomics support.
		case SUPPORTS_MULTIVIEW:
			return false; // Not available in WebGPU.
		case SUPPORTS_ATTACHMENT_VRS:
			return false; // Not available in WebGPU.
		case SUPPORTS_METALFX_SPATIAL:
		case SUPPORTS_METALFX_TEMPORAL:
			return false; // Metal-only features.
		case SUPPORTS_BUFFER_DEVICE_ADDRESS:
			return false; // Not available in WebGPU.
		case SUPPORTS_VULKAN_MEMORY_MODEL:
			return false; // Not available in WebGPU.
		case SUPPORTS_FRAMEBUFFER_DEPTH_RESOLVE:
			return false; // Not available in WebGPU.
		case SUPPORTS_POINT_SIZE:
			return false; // Point size not controllable in WebGPU.
		case SUPPORTS_SHAREABLE_TEXTURE_FORMATS:
			// WebGPU's viewFormats only allows a format and its sRGB twin -- no arbitrary
			// bit-reinterpretation between differently-shaped formats (e.g. a packed r32uint
			// viewed as rgb9e5ufloat), and several of the formats this trick is used for
			// aren't even storage-capable in WebGPU at all. See gi.cpp's SDFGI textures for
			// the one real user of this today (Task 9.5 Round 21) -- callers must fall back
			// to storing already-decoded values in a natively-supported format instead.
			return false;
		case SUPPORTS_TEXTURE_ARRAY_BINDINGS:
			// Core WebGPU has no descriptor-array equivalent at all -- one binding is
			// always exactly one resource. See webgpu_texture3d_array_inc.glsl in the
			// shader source and gi.cpp's SDFGI cascade-texture bindings (Task 9.5
			// Round 36/37) for the one real user of this today.
			return false;
		default:
			return false;
	}
}

const RDD::MultiviewCapabilities &RenderingDeviceDriverWebGPU::get_multiview_capabilities() {
	return multiview_capabilities;
}

const RDD::FragmentShadingRateCapabilities &RenderingDeviceDriverWebGPU::get_fragment_shading_rate_capabilities() {
	return fsr_capabilities;
}

const RDD::FragmentDensityMapCapabilities &RenderingDeviceDriverWebGPU::get_fragment_density_map_capabilities() {
	return fdm_capabilities;
}

String RenderingDeviceDriverWebGPU::get_api_name() const {
	return "WebGPU";
}

String RenderingDeviceDriverWebGPU::get_api_version() const {
	return "1.0";
}

String RenderingDeviceDriverWebGPU::get_pipeline_cache_uuid() const {
	return "";
}

const RDD::Capabilities &RenderingDeviceDriverWebGPU::get_capabilities() const {
	return capabilities;
}

const RenderingShaderContainerFormat &RenderingDeviceDriverWebGPU::get_shader_container_format() const {
	return *shader_container_format;
}

#endif // WEBGPU_ENABLED
