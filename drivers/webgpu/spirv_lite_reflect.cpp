/**************************************************************************/
/*  spirv_lite_reflect.cpp                                               */
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

#include "spirv_lite_reflect.h"

#include "core/templates/hash_map.h"

namespace spirv_lite_reflect {

// SPIR-V opcode / operand constants, kept local so this file stays free of any
// dependency beyond Vector and HashMap (see the header for why that matters).
static constexpr uint32_t SPIRV_MAGIC = 0x07230203u;

static constexpr uint32_t OP_TYPE_INT = 21;
static constexpr uint32_t OP_TYPE_IMAGE = 25;
static constexpr uint32_t OP_TYPE_ARRAY = 28;
static constexpr uint32_t OP_TYPE_RUNTIME_ARRAY = 29;
static constexpr uint32_t OP_TYPE_POINTER = 32;
static constexpr uint32_t OP_VARIABLE = 59;
static constexpr uint32_t OP_DECORATE = 71;

static constexpr uint32_t SC_UNIFORM_CONSTANT = 0;

static constexpr uint32_t DECO_SPEC_ID = 1;
static constexpr uint32_t DECO_BINDING = 33;
static constexpr uint32_t DECO_DESCRIPTOR_SET = 34;

Vector<RawImageDecl> extract_raw_image_decls(const Vector<uint8_t> &p_bytes) {
	Vector<RawImageDecl> result;
	const int64_t len = p_bytes.size();
	if (len < 20 || (len % 4) != 0) {
		return result;
	}
	const uint32_t *words = (const uint32_t *)p_bytes.ptr();
	const uint32_t word_count = (uint32_t)(len / 4);
	if (words[0] != SPIRV_MAGIC) {
		return result;
	}

	// First pass: the small amount of type/decoration info needed, keyed by
	// SPIR-V result <id>.
	struct ImageTypeInfo {
		uint32_t format = 0; // SPIR-V ImageFormat operand.
		uint32_t dim = 0; // SPIR-V Dim operand.
		uint32_t arrayed = 0;
		uint32_t sampled_type_id = 0; // <id> of the component (Sampled Type) operand.
	};
	HashMap<uint32_t, ImageTypeInfo> image_types;
	HashMap<uint32_t, bool> int_type_signed; // OpTypeInt result id -> Signedness
	HashMap<uint32_t, uint32_t> pointer_pointee; // UniformConstant pointer type id -> pointee type id
	HashMap<uint32_t, uint32_t> var_pointer_type; // UniformConstant variable id -> its pointer type id
	HashMap<uint32_t, uint32_t> decorate_set; // target id -> DescriptorSet value
	HashMap<uint32_t, uint32_t> decorate_binding; // target id -> Binding value
	// array type id -> element type id. A GLSL array-of-textures uniform
	// (e.g. gi.glsl's "texture3D sdf_cascades[SDFGI_MAX_CASCADES]") declares its
	// UniformConstant pointer's pointee as an OpTypeArray / OpTypeRuntimeArray
	// wrapping the real OpTypeImage, not the image type directly. Without
	// unwrapping that, the second pass's image_types lookup always misses for
	// such a binding and silently skips it.
	HashMap<uint32_t, uint32_t> array_to_elem;

	uint32_t pos = 5; // Skip the 5-word header.
	while (pos < word_count) {
		const uint32_t inst_word0 = words[pos];
		const uint32_t inst_len = inst_word0 >> 16;
		const uint32_t opcode = inst_word0 & 0xFFFFu;
		if (inst_len == 0 || pos + inst_len > word_count) {
			break; // Malformed or truncated -- bail with whatever was already found.
		}
		switch (opcode) {
			case OP_DECORATE: // <id>Target, Decoration, [operands...]
				if (inst_len >= 4) {
					const uint32_t target = words[pos + 1];
					const uint32_t decoration = words[pos + 2];
					if (decoration == DECO_DESCRIPTOR_SET) {
						decorate_set[target] = words[pos + 3];
					} else if (decoration == DECO_BINDING) {
						decorate_binding[target] = words[pos + 3];
					}
				}
				break;
			case OP_TYPE_IMAGE: // <id>Result, <id>SampledType, Dim, Depth, Arrayed, MS, Sampled, Format, [AccessQualifier]
				if (inst_len >= 9) {
					ImageTypeInfo info;
					info.sampled_type_id = words[pos + 2];
					info.dim = words[pos + 3];
					info.arrayed = words[pos + 5];
					info.format = words[pos + 8];
					image_types[words[pos + 1]] = info;
				}
				break;
			case OP_TYPE_INT: // <id>Result, Width, Signedness
				if (inst_len >= 4) {
					int_type_signed[words[pos + 1]] = words[pos + 3] != 0;
				}
				break;
			case OP_TYPE_POINTER: // <id>Result, StorageClass, <id>Type
				if (inst_len >= 4 && words[pos + 2] == SC_UNIFORM_CONSTANT) {
					pointer_pointee[words[pos + 1]] = words[pos + 3];
				}
				break;
			case OP_TYPE_ARRAY: // <id>Result, <id>ElementType, <id>Length
			case OP_TYPE_RUNTIME_ARRAY: // <id>Result, <id>ElementType
				if (inst_len >= 3) {
					array_to_elem[words[pos + 1]] = words[pos + 2];
				}
				break;
			case OP_VARIABLE: // <id>ResultType, <id>Result, StorageClass, [Initializer]
				if (inst_len >= 4 && words[pos + 3] == SC_UNIFORM_CONSTANT) {
					var_pointer_type[words[pos + 2]] = words[pos + 1];
				}
				break;
			default:
				break;
		}
		pos += inst_len;
	}

	// Second pass: resolve each UniformConstant image variable to its
	// (set, binding) and the facts about its type.
	for (const KeyValue<uint32_t, uint32_t> &kv : var_pointer_type) {
		const uint32_t *pointee = pointer_pointee.getptr(kv.value);
		if (!pointee) {
			continue;
		}
		// One level of array unwrapping is enough: SPIR-V and GLSL do not nest
		// arrays of opaque handle types.
		uint32_t elem_type_id = *pointee;
		if (const uint32_t *arr_elem = array_to_elem.getptr(elem_type_id)) {
			elem_type_id = *arr_elem;
		}
		const ImageTypeInfo *img = image_types.getptr(elem_type_id);
		if (!img) {
			continue; // A sampler or combined-image-sampler variable, not an image.
		}
		const uint32_t *set = decorate_set.getptr(kv.key);
		const uint32_t *binding = decorate_binding.getptr(kv.key);
		if (!set || !binding) {
			continue;
		}

		RawImageDecl decl;
		decl.set = *set;
		decl.binding = *binding;
		decl.spv_format = img->format;
		decl.spv_dim = img->dim;
		decl.arrayed = img->arrayed;
		// Only an integer component type is recorded. A genuine float image --
		// or one whose SampledType id cannot be resolved -- is left at -1,
		// because a float sample type is already the correct default wherever
		// this is consulted, so asserting it here would gain nothing and would
		// hide the difference between "known float" and "unknown".
		if (const bool *is_signed = int_type_signed.getptr(img->sampled_type_id)) {
			decl.int_signedness = *is_signed ? 1 : 0;
		}
		result.push_back(decl);
	}

	return result;
}

bool has_spec_id_decoration(const Vector<uint8_t> &p_bytes) {
	const int64_t len = p_bytes.size();
	if (len < 20 || (len % 4) != 0) {
		return false;
	}
	const uint32_t *words = (const uint32_t *)p_bytes.ptr();
	const uint32_t word_count = (uint32_t)(len / 4);
	if (words[0] != SPIRV_MAGIC) {
		return false;
	}

	uint32_t pos = 5; // Skip the 5-word header.
	while (pos < word_count) {
		const uint32_t inst_word0 = words[pos];
		const uint32_t inst_len = inst_word0 >> 16;
		const uint32_t opcode = inst_word0 & 0xFFFFu;
		if (inst_len == 0 || pos + inst_len > word_count) {
			break;
		}
		if (opcode == OP_DECORATE && inst_len >= 3 && words[pos + 2] == DECO_SPEC_ID) {
			return true;
		}
		pos += inst_len;
	}
	return false;
}

} // namespace spirv_lite_reflect
