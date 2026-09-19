/**************************************************************************/
/*  spirv_to_wgsl.cpp                                                     */
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

#include "spirv_to_wgsl.h"

#include "spirv_preprocess.h"
#include "tint_wrapper.h"

#include "core/error/error_macros.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

#include <cstring>
#include <vector>

namespace webgpu {

char *spirv_to_wgsl(const uint8_t *p_spv_ptr, int p_spv_size) {
	if (p_spv_size < 20 || (p_spv_size % 4) != 0) {
		return nullptr;
	}

	// Idempotent (just (re)registers a debug-printer callback), so it's safe
	// to call from every caller (runtime driver init, export-time baker)
	// without needing them to coordinate a single initialization point.
	static bool tint_initialized = false;
	if (!tint_initialized) {
		tint_wrapper_initialize();
		tint_initialized = true;
	}

	// Wrap raw bytes in a Vector for the preprocessing API.
	Vector<uint8_t> spv;
	spv.resize(p_spv_size);
	memcpy(spv.ptrw(), p_spv_ptr, p_spv_size);

	// SPIR-V preprocessing pipeline (see rendering_device_driver_webgpu.cpp
	// for the runtime Tint-fallback caller of this same sequence).
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
	spv = spirv_preprocess::strip_image_write_operands(spv);
	spv = spirv_preprocess::strip_image_fetch_read_flag_only_operands(spv);
	spv = spirv_preprocess::split_initialized_local_arrays(spv);
	spv = spirv_preprocess::strip_helper_invocation_builtin(spv);
	spv = spirv_preprocess::fold_ballot_bit_count(spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);
	spv = spirv_preprocess::strip_writeonly_storage_decoration(spv);
	spv = spirv_preprocess::eliminate_dead_resources(spv);
	spv = spirv_preprocess::eliminate_local_single_block_vars(spv);

	// Convert to uint32_t words for Tint.
	int word_count = spv.size() / 4;
	std::vector<uint32_t> spirv_words(word_count);
	memcpy(spirv_words.data(), spv.ptr(), spv.size());

	char *error_msg = nullptr;
	char *out = tint_wrapper_spirv_to_wgsl(spirv_words.data(), spirv_words.size(), &error_msg);
	if (!out) {
		if (error_msg) {
			// Truncate the error message: Tint includes the full SPIR-V disassembly
			// which floods the console/log. Keep first 1500 chars for diagnostics.
			String err_str = String::utf8(error_msg);
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

} // namespace webgpu
