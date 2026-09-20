/**************************************************************************/
/*  webgpu_spec_constant_baker_export_plugin.cpp                          */
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

#include "webgpu_spec_constant_baker_export_plugin.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "drivers/webgpu/spirv_spec_constants.h"

namespace {

// String::to_int() returns (clamping) int64_t -- not safe for the full
// unsigned 64-bit hash range Phase 1's recording writes via
// String::num_uint64() (values >= 2^63 are common; MurmurHash3 output is
// uniformly distributed across the whole 64-bit range). Plain manual decimal
// parse instead.
uint64_t parse_uint64_decimal(const String &p_str) {
	uint64_t result = 0;
	for (int i = 0; i < p_str.length(); i++) {
		char32_t c = p_str[i];
		if (c < '0' || c > '9') {
			break;
		}
		result = result * 10 + (uint64_t)(c - '0');
	}
	return result;
}

} // namespace

void WebGPUSpecConstantBakerExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	active = false;
	customization_configuration_hash = 0;
	webgpu::clear_spec_constant_usage_data();

	if (!p_features.has("shader_baker")) {
		return; // Baking itself is off (shader_baker/enabled unchecked) -- nothing to install.
	}

	Ref<EditorExportPreset> preset = get_export_preset();
	if (preset.is_null()) {
		return;
	}

	String usage_file = preset->get("shader_baker/spec_constant_usage_file");
	if (usage_file.is_empty()) {
		return; // No recording provided -- not an error, just nothing extra to bake.
	}

	Ref<FileAccess> f = FileAccess::open(usage_file, FileAccess::READ);
	if (f.is_null()) {
		WARN_PRINT(vformat("WebGPU: could not open specialization-constant usage file '%s' (error %d). Recorded shader variants will not be baked this export.", usage_file, (int)FileAccess::get_open_error()));
		return;
	}
	String text = f->get_as_text();

	// See this class's header doc comment on _get_customization_configuration_hash():
	// this is what actually forces Godot's export resource cache to re-bake
	// every shader whenever the usage file's content changes, rather than
	// silently reusing a previous export's cached (unmatched) output.
	customization_configuration_hash = text.hash64();

	Variant parsed = JSON::parse_string(text);
	if (parsed.get_type() != Variant::ARRAY) {
		WARN_PRINT(vformat("WebGPU: specialization-constant usage file '%s' is not valid JSON (expected a top-level array, as produced by godotWebGPUExportSpecConstantRecording()). Recorded shader variants will not be baked this export.", usage_file));
		return;
	}

	Array entries_array = parsed;
	Vector<webgpu::SpecConstantUsageEntry> entries;
	for (int i = 0; i < entries_array.size(); i++) {
		Dictionary entry_dict = entries_array[i];
		if (!entry_dict.has("base_spv_hash") || !entry_dict.has("constants")) {
			continue;
		}

		webgpu::SpecConstantUsageEntry entry;
		entry.base_spv_hash = parse_uint64_decimal(entry_dict["base_spv_hash"]);

		Array constants_array = entry_dict["constants"];
		for (int j = 0; j < constants_array.size(); j++) {
			Dictionary c_dict = constants_array[j];
			RDD::PipelineSpecializationConstant c;
			c.constant_id = (uint32_t)(int64_t)c_dict.get("id", 0);
			c.type = (RDD::PipelineSpecializationConstantType)(int)c_dict.get("type", 0);
			// "value" is always the raw uint32 bit pattern for the constant's
			// declared type -- see _record_spec_constant_usage()'s doc
			// comment in rendering_device_driver_webgpu.cpp for why.
			uint32_t raw_value = (uint32_t)(int64_t)c_dict.get("value", 0);
			switch (c.type) {
				case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_BOOL:
					c.bool_value = raw_value != 0;
					break;
				case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_INT:
					c.int_value = raw_value;
					break;
				case RDD::PIPELINE_SPECIALIZATION_CONSTANT_TYPE_FLOAT:
					memcpy(&c.float_value, &raw_value, sizeof(float));
					break;
			}
			entry.constants.push_back(c);
		}

		entries.push_back(entry);
	}

	webgpu::set_spec_constant_usage_table(entries);
	active = true;
	print_verbose(vformat("WebGPU: installed %d specialization-constant usage entries from '%s' for export-time baking.", entries.size(), usage_file));
}

bool WebGPUSpecConstantBakerExportPlugin::_begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) {
	// Only registers for the _end_customize_resources() timing hook (see the
	// header's doc comment) -- this plugin doesn't customize any resource
	// itself, so _customize_resource() is left at its no-op default.
	return active;
}

void WebGPUSpecConstantBakerExportPlugin::_end_customize_resources() {
	if (!active) {
		webgpu::clear_spec_constant_usage_data();
		return;
	}

	// Grabbed before clear_spec_constant_usage_data() below, which wipes the
	// table this reads from.
	webgpu::SpecConstantMatchStats match_stats = webgpu::get_spec_constant_usage_match_stats();

	Vector<webgpu::BakedSpecConstantVariant> variants = webgpu::take_baked_spec_constant_variants();
	webgpu::clear_spec_constant_usage_data();
	active = false;

	// See spirv_spec_constants.h's doc comment on this diagnostic: a low
	// match rate almost always means the usage file was captured against a
	// different build's SPIR-V (observed: rebuilding the editor alone can
	// regenerate .godot/shader_cache non-deterministically) and needs
	// re-capturing against the current build to actually help. A real
	// WARN_PRINT (not print_verbose) since this is worth surfacing even
	// without --verbose -- it explains why baking silently did less than
	// expected, which is otherwise easy to miss.
	if (match_stats.total_hashes > 0 && match_stats.matched_hashes < match_stats.total_hashes) {
		WARN_PRINT(vformat(
				"WebGPU: only %d of %d distinct shader(s) from the specialization-constant usage file were found in this export (%d unmatched). "
				"This usually means the usage file was captured against a different build's shaders -- re-capture it (open the exported debug build, "
				"set window.GODOT_WEBGPU_RECORD_SPEC_CONSTANTS = true in the browser console, play through the project, then call "
				"godotWebGPUExportSpecConstantRecording() to download a fresh one) against the current export before re-exporting for full coverage.",
				match_stats.matched_hashes, match_stats.total_hashes, match_stats.total_hashes - match_stats.matched_hashes));
	}

	if (variants.is_empty()) {
		return; // Nothing baked (usage file had no matches, or all failed) -- no file needed.
	}

	// Serialize into the shared binary format -- see
	// spirv_spec_constants.h's BAKED_SPEC_VARIANTS_PATH doc comment for the
	// exact layout, which rendering_device_driver_webgpu.cpp's
	// _get_baked_spec_constant_variants() reads back.
	Vector<CharString> wgsl_utf8;
	wgsl_utf8.resize(variants.size());
	uint32_t total_size = sizeof(uint32_t) * 2; // magic + count
	for (int i = 0; i < variants.size(); i++) {
		wgsl_utf8.write[i] = variants[i].wgsl.utf8();
		total_size += sizeof(uint64_t) + sizeof(uint32_t) + (uint32_t)wgsl_utf8[i].length();
	}

	PackedByteArray bytes;
	bytes.resize(total_size);
	uint8_t *w = bytes.ptrw();
	uint32_t offset = 0;

	uint32_t magic = webgpu::BAKED_SPEC_VARIANTS_MAGIC;
	memcpy(w + offset, &magic, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	uint32_t count = (uint32_t)variants.size();
	memcpy(w + offset, &count, sizeof(uint32_t));
	offset += sizeof(uint32_t);

	for (int i = 0; i < variants.size(); i++) {
		uint64_t combo_hash = variants[i].combo_hash;
		memcpy(w + offset, &combo_hash, sizeof(uint64_t));
		offset += sizeof(uint64_t);

		uint32_t len = (uint32_t)wgsl_utf8[i].length();
		memcpy(w + offset, &len, sizeof(uint32_t));
		offset += sizeof(uint32_t);

		if (len > 0) {
			memcpy(w + offset, wgsl_utf8[i].get_data(), len);
			offset += len;
		}
	}

	add_file(webgpu::BAKED_SPEC_VARIANTS_PATH, bytes, false);
	print_verbose(vformat("WebGPU: baked %d specialization-constant shader variant(s) into %s.", variants.size(), String(webgpu::BAKED_SPEC_VARIANTS_PATH)));
}
