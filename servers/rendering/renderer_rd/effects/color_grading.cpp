/**************************************************************************/
/*  color_grading.cpp                                                     */
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

#include "color_grading.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/storage/environment_storage.h"

using namespace RendererRD;

ColorGrading::ColorGrading() {
	Vector<String> modes;
	modes.push_back("\n"); // MODE_USER_LUT_3D
	modes.push_back("\n#define MODE_USER_LUT_1D\n"); // MODE_USER_LUT_1D
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

ColorGrading::~ColorGrading() {
	for (KeyValue<RID, Entry> &E : entries) {
		_free_entry(E.value);
	}
	entries.clear();
	shader.version_free(shader_version);
}

void ColorGrading::_free_entry(Entry &r_entry) {
	if (r_entry.grading.is_valid()) {
		RD::get_singleton()->free_rid(r_entry.grading);
	}
	for (Output &output : r_entry.outputs) {
		if (output.texture.is_valid()) {
			RD::get_singleton()->free_rid(output.texture);
		}
	}
}

RID ColorGrading::_create_lut_texture(uint32_t p_usage, const Vector<uint8_t> &p_data) {
	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_3D;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = RendererColorGrading::LUT_SIZE;
	tf.height = RendererColorGrading::LUT_SIZE;
	tf.depth = RendererColorGrading::LUT_SIZE;
	tf.usage_bits = p_usage;
	Vector<Vector<uint8_t>> data;
	if (!p_data.is_empty()) {
		data.push_back(p_data);
	}
	RID texture = RD::get_singleton()->texture_create(tf, RD::TextureView(), data);
	RD::get_singleton()->set_resource_name(texture, "Color grading LUT");
	return texture;
}

RID ColorGrading::get_lut(RID p_environment, RID p_user_lut, bool p_user_lut_1d, bool p_linear_output, bool p_update) {
	RendererEnvironmentStorage *environment_storage = RendererEnvironmentStorage::get_singleton();

	if (p_update) {
		// Entries of freed environments go here, where nothing else is using them (RIDs are never reused).
		Vector<RID> stale;
		for (const KeyValue<RID, Entry> &E : entries) {
			if (E.key != p_environment && !environment_storage->is_environment(E.key)) {
				stale.push_back(E.key);
			}
		}
		for (const RID &rid : stale) {
			_free_entry(entries[rid]);
			entries.erase(rid);
		}
	}

	if (!environment_storage->is_environment(p_environment) || !environment_storage->environment_get_color_grading_enabled(p_environment)) {
		return RID();
	}

	const uint64_t version = environment_storage->environment_get_color_grading_version(p_environment);
	Entry *entry = entries.getptr(p_environment);
	if (!entry) {
		if (!p_update) {
			return RID();
		}
		entry = &entries.insert(p_environment, Entry())->value;
	}

	if (entry->version != version) {
		if (!p_update) {
			return RID();
		}
		// The bake, on the CPU: ~36k grades, about a millisecond, once per change.
		const Vector<uint8_t> data = environment_storage->environment_bake_color_grading(p_environment);
		if (entry->grading.is_null()) {
			entry->grading = _create_lut_texture(RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT, data);
		} else {
			RD::get_singleton()->texture_update(entry->grading, 0, data);
		}
		entry->version = version;
	}

	Output &output = entry->outputs[p_linear_output ? 1 : 0];
	if (output.texture.is_valid() && output.version == version && output.user_lut == p_user_lut && output.user_lut_1d == p_user_lut_1d) {
		return output.texture;
	}
	if (!p_update) {
		return RID();
	}
	if (output.texture.is_null()) {
		output.texture = _create_lut_texture(RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT, Vector<uint8_t>());
	}

	const Mode mode = p_user_lut.is_valid() && p_user_lut_1d ? MODE_USER_LUT_1D : MODE_USER_LUT_3D;
	PushConstant push_constant = {};
	push_constant.size = RendererColorGrading::LUT_SIZE;
	RID user_lut = p_user_lut;
	if (user_lut.is_valid()) {
		push_constant.flags |= FLAG_USE_USER_LUT;
	} else {
		user_lut = TextureStorage::get_singleton()->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_3D_WHITE);
		if (p_linear_output) {
			push_constant.flags |= FLAG_LINEARIZE;
		}
	}

	// The same sampler the tonemap pass samples the user LUT with.
	RID sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID nearest_sampler = MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform u_grading(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest_sampler, entry->grading }));
	RD::Uniform u_user_lut(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, user_lut }));
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 2, output.texture);

	RID shader_rd = shader.version_get_shader(shader_version, mode);
	RD::get_singleton()->draw_command_begin_label("Color Grading LUT");
	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[mode]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, UniformSetCacheRD::get_singleton()->get_cache(shader_rd, 0, u_grading, u_user_lut, u_dest), 0);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, RendererColorGrading::LUT_SIZE, RendererColorGrading::LUT_SIZE, RendererColorGrading::LUT_SIZE);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();

	output.version = version;
	output.user_lut = p_user_lut;
	output.user_lut_1d = p_user_lut_1d;
	return output.texture;
}
