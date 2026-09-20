/**************************************************************************/
/*  spirv_spec_constants.cpp                                              */
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

#include "spirv_spec_constants.h"

#include "core/os/mutex.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"

namespace webgpu {

namespace {

constexpr uint16_t SPV_OP_DECORATE = 71;
constexpr uint16_t SPV_OP_SPEC_CONSTANT_TRUE = 48;
constexpr uint16_t SPV_OP_SPEC_CONSTANT_FALSE = 49;
constexpr uint16_t SPV_OP_SPEC_CONSTANT = 50;
constexpr uint16_t SPV_DECORATION_SPEC_ID = 1;

inline uint32_t spv_read_word(const uint8_t *p_data, uint32_t p_word_index) {
	const uint8_t *p = p_data + p_word_index * 4;
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

inline void spv_write_word(uint8_t *p_data, uint32_t p_word_index, uint32_t p_value) {
	uint8_t *p = p_data + p_word_index * 4;
	p[0] = p_value & 0xFF;
	p[1] = (p_value >> 8) & 0xFF;
	p[2] = (p_value >> 16) & 0xFF;
	p[3] = (p_value >> 24) & 0xFF;
}

// Raw uint32 bit pattern for a specialization constant's value, regardless of
// its declared type -- bool as 0/1, int as-is, float via memcpy. Shared by
// patch_spirv_spec_constants() and hash_spec_constant_combo() so both agree
// on what a given constant "is" bit-for-bit.
uint32_t spec_constant_raw_value(const RDD::PipelineSpecializationConstant &p_constant) {
	uint32_t val = 0;
	switch (p_constant.type) {
		case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
			val = p_constant.bool_value ? 1 : 0;
			break;
		case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
			val = (uint32_t)p_constant.int_value;
			break;
		case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
			memcpy(&val, &p_constant.float_value, sizeof(float));
			break;
	}
	return val;
}

} // namespace

PackedByteArray patch_spirv_spec_constants(const PackedByteArray &p_spirv, VectorView<RDD::PipelineSpecializationConstant> p_constants) {
	if (p_constants.size() == 0 || p_spirv.size() < 20) {
		return p_spirv;
	}

	// Build a map: SpecId → value (as uint32_t).
	HashMap<uint32_t, uint32_t> spec_values;
	for (uint32_t i = 0; i < p_constants.size(); i++) {
		const RDD::PipelineSpecializationConstant &c = p_constants[i];
		spec_values[c.constant_id] = spec_constant_raw_value(c);
	}

	// First pass: scan OpDecorate for SpecId → result_id mapping.
	HashMap<uint32_t, uint32_t> spec_id_to_result_id; // SpecId → result_id
	uint32_t total_words = p_spirv.size() / 4;
	uint32_t pos = 5; // Skip SPIR-V header (5 words).
	while (pos < total_words) {
		uint32_t w0 = spv_read_word(p_spirv.ptr(), pos);
		uint32_t wc = w0 >> 16;
		uint32_t op = w0 & 0xFFFF;
		if (wc == 0 || pos + wc > total_words) {
			break;
		}
		// OpDecorate target decoration [literal...]
		// For SpecId: OpDecorate result_id SpecId(1) literal_spec_id
		if (op == SPV_OP_DECORATE && wc >= 4) {
			uint32_t target = spv_read_word(p_spirv.ptr(), pos + 1);
			uint32_t decoration = spv_read_word(p_spirv.ptr(), pos + 2);
			if (decoration == SPV_DECORATION_SPEC_ID) {
				uint32_t spec_id = spv_read_word(p_spirv.ptr(), pos + 3);
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
		uint32_t w0 = spv_read_word(out.ptr(), pos);
		uint32_t wc = w0 >> 16;
		uint32_t op = w0 & 0xFFFF;
		if (wc == 0 || pos + wc > total_words) {
			break;
		}

		if (op == SPV_OP_SPEC_CONSTANT_TRUE || op == SPV_OP_SPEC_CONSTANT_FALSE) {
			// OpSpecConstantTrue/False: wc=3, [type_id, result_id]
			if (wc >= 3) {
				uint32_t result_id = spv_read_word(out.ptr(), pos + 2);
				if (result_to_value.has(result_id)) {
					uint32_t val = result_to_value[result_id];
					uint16_t new_op = val ? SPV_OP_SPEC_CONSTANT_TRUE : SPV_OP_SPEC_CONSTANT_FALSE;
					spv_write_word(out.ptrw(), pos, (wc << 16) | new_op);
				}
			}
		} else if (op == SPV_OP_SPEC_CONSTANT) {
			// OpSpecConstant: wc>=4, [type_id, result_id, value...]
			if (wc >= 4) {
				uint32_t result_id = spv_read_word(out.ptr(), pos + 2);
				if (result_to_value.has(result_id)) {
					spv_write_word(out.ptrw(), pos + 3, result_to_value[result_id]);
				}
			}
		}

		pos += wc;
	}

	return out;
}

uint64_t hash_spirv(const uint8_t *p_spirv_ptr, int p_spirv_size) {
	uint32_t hash_lo = hash_murmur3_buffer(p_spirv_ptr, p_spirv_size);
	uint32_t hash_hi = hash_murmur3_buffer(p_spirv_ptr, p_spirv_size, 0x9E3779B9);
	return ((uint64_t)hash_hi << 32) | hash_lo;
}

uint64_t hash_spec_constant_combo(uint64_t p_base_spirv_hash, VectorView<RDD::PipelineSpecializationConstant> p_constants) {
	// Same "two independent 32-bit rolling hashes, different seeds, combined
	// into 64 bits" shape as hash_spirv() above -- one chain seeded from the
	// base hash's low half, one from its high half (further mixed with a
	// different constant), each folding in every (constant_id, raw value)
	// pair in order.
	uint32_t lo = hash_murmur3_one_64(p_base_spirv_hash, HASH_MURMUR3_SEED);
	uint32_t hi = hash_murmur3_one_64(p_base_spirv_hash, 0x9E3779B9);
	for (uint32_t i = 0; i < p_constants.size(); i++) {
		const RDD::PipelineSpecializationConstant &c = p_constants[i];
		uint64_t word = (((uint64_t)c.constant_id) << 32) | spec_constant_raw_value(c);
		lo = hash_murmur3_one_64(word, lo);
		hi = hash_murmur3_one_64(word, hi);
	}
	return ((uint64_t)hi << 32) | lo;
}

namespace {
Mutex spec_constant_usage_mutex;
HashMap<uint64_t, Vector<Vector<RDD::PipelineSpecializationConstant>>> spec_constant_usage_table;
HashSet<uint64_t> spec_constant_usage_matched_hashes;

Mutex baked_spec_constant_variants_mutex;
Vector<BakedSpecConstantVariant> baked_spec_constant_variants;
} // namespace

void set_spec_constant_usage_table(const Vector<SpecConstantUsageEntry> &p_entries) {
	MutexLock lock(spec_constant_usage_mutex);
	spec_constant_usage_table.clear();
	spec_constant_usage_matched_hashes.clear();
	for (const SpecConstantUsageEntry &entry : p_entries) {
		spec_constant_usage_table[entry.base_spv_hash].push_back(entry.constants);
	}
}

void clear_spec_constant_usage_data() {
	{
		MutexLock lock(spec_constant_usage_mutex);
		spec_constant_usage_table.clear();
		spec_constant_usage_matched_hashes.clear();
	}
	{
		MutexLock lock(baked_spec_constant_variants_mutex);
		baked_spec_constant_variants.clear();
	}
}

Vector<Vector<RDD::PipelineSpecializationConstant>> get_spec_constant_usage_for_hash(uint64_t p_base_spv_hash) {
	MutexLock lock(spec_constant_usage_mutex);
	const Vector<Vector<RDD::PipelineSpecializationConstant>> *found = spec_constant_usage_table.getptr(p_base_spv_hash);
	if (!found) {
		return Vector<Vector<RDD::PipelineSpecializationConstant>>();
	}
	spec_constant_usage_matched_hashes.insert(p_base_spv_hash);
	return *found;
}

SpecConstantMatchStats get_spec_constant_usage_match_stats() {
	MutexLock lock(spec_constant_usage_mutex);
	SpecConstantMatchStats stats;
	stats.matched_hashes = (uint32_t)spec_constant_usage_matched_hashes.size();
	stats.total_hashes = (uint32_t)spec_constant_usage_table.size();
	return stats;
}

void record_baked_spec_constant_variant(uint64_t p_combo_hash, const String &p_wgsl) {
	MutexLock lock(baked_spec_constant_variants_mutex);
	BakedSpecConstantVariant variant;
	variant.combo_hash = p_combo_hash;
	variant.wgsl = p_wgsl;
	baked_spec_constant_variants.push_back(variant);
}

Vector<BakedSpecConstantVariant> take_baked_spec_constant_variants() {
	MutexLock lock(baked_spec_constant_variants_mutex);
	Vector<BakedSpecConstantVariant> result = baked_spec_constant_variants;
	baked_spec_constant_variants.clear();
	return result;
}

} // namespace webgpu
