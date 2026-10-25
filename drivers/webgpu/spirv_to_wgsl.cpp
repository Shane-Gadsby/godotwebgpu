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

#include "core/error/error_macros.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"
#include "drivers/webgpu/spirv_preprocess.h"
#ifdef WGSL_BACKEND_TINT
#include "drivers/webgpu/tint_wrapper.h"
#else
#include "drivers/webgpu/spirv_cross_wrapper.h"
#endif

#include <cstring>
#include <string>
#include <vector>

namespace webgpu {

char *spirv_to_wgsl(const uint8_t *p_spv_ptr, int p_spv_size) {
	if (p_spv_size < 20 || (p_spv_size % 4) != 0) {
		return nullptr;
	}

	// Idempotent (just (re)registers a debug-printer callback), so it's safe
	// to call from every caller (runtime driver init, export-time baker)
	// without needing them to coordinate a single initialization point.
#ifdef WGSL_BACKEND_TINT
	static bool tint_initialized = false;
	if (!tint_initialized) {
		tint_wrapper_initialize();
		tint_initialized = true;
	}
#endif

	// Wrap raw bytes in a Vector for the preprocessing API.
	Vector<uint8_t> spv;
	spv.resize(p_spv_size);
	memcpy(spv.ptrw(), p_spv_ptr, p_spv_size);

	std::string pipeline_error;
	spv = spirv_preprocess::run_pipeline(spv, &pipeline_error);
	if (spv.is_empty()) {
		ERR_PRINT(vformat("SPIR-V→WGSL failed: %s", String::utf8(pipeline_error.c_str())));
		return nullptr;
	}

	// Convert to uint32_t words for Tint.
	int word_count = spv.size() / 4;
	std::vector<uint32_t> spirv_words(word_count);
	memcpy(spirv_words.data(), spv.ptr(), spv.size());

	char *error_msg = nullptr;
#ifdef WGSL_BACKEND_TINT
	const char *backend_name = "Tint";
	char *out = tint_wrapper_spirv_to_wgsl(spirv_words.data(), spirv_words.size(), &error_msg);
#else
	const char *backend_name = "SPIRV-Cross";
	char *out = spirv_cross_wrapper_spirv_to_wgsl(spirv_words.data(), spirv_words.size(), &error_msg);
#endif
	if (!out) {
		if (error_msg) {
			// Truncate the error message: Tint includes the full SPIR-V disassembly
			// which floods the console/log. Keep first 1500 chars for diagnostics.
			String err_str = String::utf8(error_msg);
			err_str = err_str.replace("\n", " | ");
			if (err_str.length() > 1500) {
				err_str = err_str.left(1500) + "... [truncated]";
			}
			ERR_PRINT(vformat("%s SPIR-V→WGSL failed: %s", backend_name, err_str));
			free(error_msg);
		} else {
			ERR_PRINT(vformat("%s SPIR-V→WGSL failed (unknown error)", backend_name));
		}
		return nullptr;
	}
	return out;
}

} // namespace webgpu
