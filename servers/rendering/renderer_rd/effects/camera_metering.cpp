/**************************************************************************/
/*  camera_metering.cpp                                                   */
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

#include "camera_metering.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

CameraMetering::CameraMetering() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_METER_READ\n"); // MODE_METER_READ
	modes.push_back("\n#define MODE_REDUCE\n"); // MODE_REDUCE
	modes.push_back("\n#define MODE_METER_RESOLVE\n"); // MODE_METER_RESOLVE
	modes.push_back("\n#define MODE_HISTOGRAM\n"); // MODE_HISTOGRAM
	modes.push_back("\n#define MODE_HISTOGRAM_RESOLVE\n"); // MODE_HISTOGRAM_RESOLVE
	modes.push_back("\n#define MODE_AWB_READ\n"); // MODE_AWB_READ
	modes.push_back("\n#define MODE_AWB_RESOLVE\n"); // MODE_AWB_RESOLVE
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

CameraMetering::~CameraMetering() {
	shader.version_free(shader_version);
}

void CameraMetering::MeteringBuffers::configure(RenderSceneBuffersRD *p_render_buffers) {
	Size2i size = p_render_buffers->get_internal_size();
	do {
		size = Size2i(MAX((size.x + 7) / 8, 1), MAX((size.y + 7) / 8, 1));
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
		tf.width = size.x;
		tf.height = size.y;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT;
		sums.push_back(RD::get_singleton()->texture_create(tf, RD::TextureView()));
		sums_sizes.push_back(size);
	} while (size.x > 1 || size.y > 1);

	RD::TextureFormat tf;
	tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
	tf.width = 2;
	tf.height = 1;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	for (int i = 0; i < 2; i++) {
		awb_state[i] = RD::get_singleton()->texture_create(tf, RD::TextureView());
		// Texel 1's z = 0 marks the state invalid: the next update starts over.
		RD::get_singleton()->texture_clear(awb_state[i], Color(0.0, 0.0, 0.0, 0.0), 0u, 1u, 0u, 1u);
	}
	awb_current = 0;
}

void CameraMetering::MeteringBuffers::free_data() {
	for (int i = 0; i < sums.size(); i++) {
		RD::get_singleton()->free_rid(sums[i]);
	}
	sums.clear();
	sums_sizes.clear();
	if (histogram.is_valid()) {
		RD::get_singleton()->free_rid(histogram);
		histogram = RID();
	}
	for (int i = 0; i < 2; i++) {
		if (awb_state[i].is_valid()) {
			RD::get_singleton()->free_rid(awb_state[i]);
			awb_state[i] = RID();
		}
	}
}

Ref<CameraMetering::MeteringBuffers> CameraMetering::get_buffers(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers->has_custom_data(RB_CAMERA_METERING_BUFFERS)) {
		return p_render_buffers->get_custom_data(RB_CAMERA_METERING_BUFFERS);
	}
	Ref<MeteringBuffers> buffers;
	buffers.instantiate();
	buffers->configure(p_render_buffers.ptr());
	p_render_buffers->set_custom_data(RB_CAMERA_METERING_BUFFERS, buffers);
	return buffers;
}

void CameraMetering::_reduce(RD::ComputeListID p_compute_list, Mode p_read_mode, RID p_read_set0, const Size2i &p_source_size, Ref<MeteringBuffers> p_buffers, PushConstant &r_push_constant) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();

	r_push_constant.source_size[0] = p_source_size.x;
	r_push_constant.source_size[1] = p_source_size.y;
	RID read_shader = shader.version_get_shader(shader_version, p_read_mode);
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->sums[0]);
	RD::get_singleton()->compute_list_bind_compute_pipeline(p_compute_list, pipelines[p_read_mode]);
	RD::get_singleton()->compute_list_bind_uniform_set(p_compute_list, p_read_set0, 0);
	RD::get_singleton()->compute_list_bind_uniform_set(p_compute_list, uniform_set_cache->get_cache(read_shader, 1, u_dest), 1);
	RD::get_singleton()->compute_list_set_push_constant(p_compute_list, &r_push_constant, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(p_compute_list, p_source_size.x, p_source_size.y, 1);

	RID reduce_shader = shader.version_get_shader(shader_version, MODE_REDUCE);
	for (int i = 1; i < p_buffers->sums.size(); i++) {
		RD::get_singleton()->compute_list_add_barrier(p_compute_list);
		const Size2i source_size = p_buffers->sums_sizes[i - 1];
		r_push_constant.source_size[0] = source_size.x;
		r_push_constant.source_size[1] = source_size.y;
		RD::Uniform u_source(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->sums[i - 1]);
		RD::Uniform u_level_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->sums[i]);
		RD::get_singleton()->compute_list_bind_compute_pipeline(p_compute_list, pipelines[MODE_REDUCE]);
		RD::get_singleton()->compute_list_bind_uniform_set(p_compute_list, uniform_set_cache->get_cache(reduce_shader, 0, u_source), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(p_compute_list, uniform_set_cache->get_cache(reduce_shader, 1, u_level_dest), 1);
		RD::get_singleton()->compute_list_set_push_constant(p_compute_list, &r_push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(p_compute_list, source_size.x, source_size.y, 1);
	}
	RD::get_singleton()->compute_list_add_barrier(p_compute_list);
}

void CameraMetering::meter_exposure(RID p_source, const Size2i &p_source_size, Ref<Luminance::LuminanceBuffers> p_luminance, Ref<MeteringBuffers> p_buffers, const ExposureSettings &p_settings) {
	ERR_FAIL_COND(p_luminance.is_null() || p_buffers.is_null() || p_luminance->reduce.is_empty() || p_buffers->sums.is_empty());
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	PushConstant push_constant = {};
	push_constant.adjust = p_settings.adjust;
	push_constant.flags = p_settings.set ? FLAG_SET : 0;
	if (p_settings.metering.log_average) {
		push_constant.flags |= FLAG_LOG_AVERAGE;
	}
	push_constant.min_luminance = p_settings.min_luminance;
	push_constant.max_luminance = p_settings.max_luminance;
	push_constant.highlight_percentile = p_settings.metering.highlight_percentile;
	push_constant.highlight_key = p_settings.metering.highlight_key;
	push_constant.center[0] = p_settings.metering.center.x;
	push_constant.center[1] = p_settings.metering.center.y;
	push_constant.radius = p_settings.metering.radius;
	push_constant.inner_weight = p_settings.metering.inner_weight;
	push_constant.outer_weight = p_settings.metering.outer_weight;

	// The resolve writes the luminance texture the reduction's last level would have, which is then swapped in as the
	// current one, exactly as Luminance::luminance_reduction() ends.
	RID dest = p_luminance->reduce[p_luminance->reduce.size() - 1];
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_prev_luminance(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, p_luminance->current }));
	RD::Uniform u_dest_luminance(RD::UNIFORM_TYPE_IMAGE, 0, dest);

	const bool histogram = p_settings.metering.mode == RSE::CAMERA_ATTRIBUTES_METERING_HIGHLIGHT_WEIGHTED;
	if (histogram) {
		if (p_buffers->histogram.is_null()) {
			p_buffers->histogram = RD::get_singleton()->storage_buffer_create(HISTOGRAM_BINS * sizeof(uint32_t));
		}
		RD::get_singleton()->buffer_clear(p_buffers->histogram, 0, HISTOGRAM_BINS * sizeof(uint32_t));
	}

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	if (histogram) {
		push_constant.source_size[0] = p_source_size.x;
		push_constant.source_size[1] = p_source_size.y;
		RID count_shader = shader.version_get_shader(shader_version, MODE_HISTOGRAM);
		RD::Uniform u_histogram_out(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_buffers->histogram);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_HISTOGRAM]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(count_shader, 0, u_source), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(count_shader, 1, u_histogram_out), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_source_size.x, p_source_size.y, 1);
		RD::get_singleton()->compute_list_add_barrier(compute_list);

		RID resolve_shader = shader.version_get_shader(shader_version, MODE_HISTOGRAM_RESOLVE);
		RD::Uniform u_histogram_in(RD::UNIFORM_TYPE_STORAGE_BUFFER, 0, p_buffers->histogram);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_HISTOGRAM_RESOLVE]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 0, u_histogram_in, u_prev_luminance), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 1, u_dest_luminance), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch(compute_list, 1, 1, 1);
	} else {
		RID read_shader = shader.version_get_shader(shader_version, MODE_METER_READ);
		_reduce(compute_list, MODE_METER_READ, uniform_set_cache->get_cache(read_shader, 0, u_source), p_source_size, p_buffers, push_constant);

		RID resolve_shader = shader.version_get_shader(shader_version, MODE_METER_RESOLVE);
		RD::Uniform u_sums(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->sums[p_buffers->sums.size() - 1]);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_METER_RESOLVE]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 0, u_sums, u_prev_luminance), 0);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 1, u_dest_luminance), 1);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch(compute_list, 1, 1, 1);
	}
	RD::get_singleton()->compute_list_end();

	SWAP(p_luminance->current, p_luminance->reduce.write[p_luminance->reduce.size() - 1]);
}

RID CameraMetering::auto_white_balance(RID p_source, const Size2i &p_source_size, Ref<MeteringBuffers> p_buffers, const WhiteBalanceSettings &p_settings) {
	ERR_FAIL_COND_V(p_buffers.is_null() || p_buffers->sums.is_empty(), RID());
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	PushConstant push_constant = {};
	// An exponential approach with a time constant of 1 / speed, the same at any frame rate.
	push_constant.adjust = 1.0 - Math::exp(-double(p_settings.awb.speed) * MAX(p_settings.time_step, 0.0));
	push_constant.flags = 0;
	if (p_settings.awb.version != p_buffers->awb_version) {
		p_buffers->awb_version = p_settings.awb.version;
		push_constant.flags |= FLAG_SET; // Turned on again: start over from the manual white balance alone.
	}
	if (p_settings.use_auto_exposure && p_settings.auto_exposure_texture.is_valid()) {
		push_constant.flags |= FLAG_USE_AUTO_EXPOSURE;
	}
	push_constant.exposure = p_settings.exposure;
	push_constant.auto_exposure_scale = p_settings.auto_exposure_scale;
	push_constant.luminance_multiplier = p_settings.luminance_multiplier;
	push_constant.min_temperature = p_settings.awb.min_temperature;
	push_constant.max_temperature = p_settings.awb.max_temperature;

	RID exposure_texture = (push_constant.flags & FLAG_USE_AUTO_EXPOSURE) ? p_settings.auto_exposure_texture : TextureStorage::get_singleton()->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_WHITE);
	RID prev = p_buffers->awb_state[p_buffers->awb_current];
	RID next = p_buffers->awb_state[1 - p_buffers->awb_current];

	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_prev_state(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, prev }));
	RD::Uniform u_exposure(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ sampler, exposure_texture }));

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RID read_shader = shader.version_get_shader(shader_version, MODE_AWB_READ);
	_reduce(compute_list, MODE_AWB_READ, uniform_set_cache->get_cache(read_shader, 0, u_source, u_prev_state, u_exposure), p_source_size, p_buffers, push_constant);

	RID resolve_shader = shader.version_get_shader(shader_version, MODE_AWB_RESOLVE);
	RD::Uniform u_sums(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->sums[p_buffers->sums.size() - 1]);
	RD::Uniform u_next_state(RD::UNIFORM_TYPE_IMAGE, 0, next);
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_AWB_RESOLVE]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 0, u_sums, u_prev_state), 0);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(resolve_shader, 1, u_next_state), 1);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch(compute_list, 1, 1, 1);
	RD::get_singleton()->compute_list_end();

	p_buffers->awb_current = 1 - p_buffers->awb_current;
	return next;
}
