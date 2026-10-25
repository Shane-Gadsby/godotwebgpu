/**************************************************************************/
/*  fuzz_preprocess_passes.cpp                                            */
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

// Fuzz every SPIR-V preprocessing pass, in run_pipeline()'s order, on raw
// bytes.
//
// Deliberately skips run_pipeline()'s input validation: the raw word passes
// must handle arbitrary words on their own. The passes that build SPIRV-Tools'
// optimizer IR only get a module the validator accepts, as in run_pipeline():
// their own check_ir_preconditions() keeps out what the IR loader asserts on,
// but the optimizer also asserts on type errors (an OpLoad from a non-pointer,
// found here), which only the validator rules out. fuzz_spirv_to_wgsl covers
// the validated pipeline end to end, where a valid module that a raw pass
// leaves invalid shows up as an optimizer assert.

#include "../../spirv_opt_passes.h"
#include "../../spirv_preprocess.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

static bool valid(const Vector<uint8_t> &p_bytes) {
	return p_bytes.size() >= 20 && (p_bytes.size() % 4) == 0 &&
			spirv_opt_passes::validate_module(reinterpret_cast<const uint32_t *>(p_bytes.ptr()), (size_t)p_bytes.size() / 4);
}

static Vector<uint8_t> run_opt_pass(const Vector<uint8_t> &p_bytes, bool (*p_pass)(std::vector<uint32_t> &)) {
	if (!valid(p_bytes)) {
		return p_bytes;
	}
	std::vector<uint32_t> words(p_bytes.size() / 4);
	memcpy(words.data(), p_bytes.ptr(), p_bytes.size());
	if (!p_pass(words)) {
		return p_bytes;
	}
	Vector<uint8_t> out;
	out.resize((int64_t)words.size() * 4);
	memcpy(out.ptrw(), words.data(), words.size() * 4);
	return out;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	Vector<uint8_t> spv;
	spv.resize((int64_t)size);
	if (size > 0) {
		memcpy(spv.ptrw(), data, size);
	}

	// Same order as spirv_preprocess::run_pipeline(), keep them in step.
	if (valid(spv)) {
		spv = spirv_preprocess::inline_opaque_functions(spv);
	}
	if (!spirv_preprocess::spec_constants_overridable(spv)) {
		spv = spirv_preprocess::freeze_spec_constant_ops(spv);
	}
	spv = spirv_preprocess::rewrite_copy_logical(spv);
	spv = spirv_preprocess::rewrite_terminate_invocation(spv);
	spv = spirv_preprocess::convert_push_constants_to_uniforms(spv);
	spv = spirv_preprocess::split_combined_samplers(spv);
	spv = spirv_preprocess::fix_depth2_images(spv).bytes;
	spv = spirv_preprocess::negate_position_y(spv);
	spv = spirv_preprocess::strip_unsupported_decorations(spv);
	spv = spirv_preprocess::strip_memory_barrier(spv);
	spv = spirv_preprocess::strip_image_write_operands(spv);
	spv = spirv_preprocess::strip_image_fetch_read_flag_only_operands(spv);
	spv = spirv_preprocess::hoist_constant_local_arrays(spv);
	spv = spirv_preprocess::split_initialized_local_arrays(spv);
	spv = spirv_preprocess::strip_helper_invocation_builtin(spv);
	spv = spirv_preprocess::fold_ballot_bit_count(spv);
	spv = spirv_preprocess::lower_subgroup_ops(spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	spv = run_opt_pass(spv, spirv_opt_passes::lower_tint_unsupported_ops);
	spv = run_opt_pass(spv, spirv_opt_passes::apply_sampler_lod_bias);
	spv = run_opt_pass(spv, spirv_opt_passes::expand_binding_arrays);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);
	spv = spirv_preprocess::strip_writeonly_storage_decoration(spv);
	if (valid(spv)) {
		spv = spirv_preprocess::eliminate_dead_resources(spv);
	}
	if (valid(spv)) {
		spv = spirv_preprocess::eliminate_local_single_block_vars(spv);
	}

	return 0;
}
