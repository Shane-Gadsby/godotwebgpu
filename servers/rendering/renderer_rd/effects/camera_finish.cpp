/**************************************************************************/
/*  camera_finish.cpp                                                     */
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

#include "camera_finish.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

CameraFinish::CameraFinish() {
	{
		Vector<String> modes;
		modes.push_back("\n");
		finish_shader.initialize(modes);
		finish_shader_version = finish_shader.version_create();
		finish_pipeline.setup(finish_shader.version_get_shader(finish_shader_version, 0), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), RD::PipelineColorBlendState::create_disabled(), 0);
	}
	{
		Vector<String> modes;
		modes.push_back("\n"); // HISTOGRAM_COUNT
		modes.push_back("\n#define MODE_MAX\n"); // HISTOGRAM_MAX
		histogram_shader.initialize(modes);
		histogram_shader_version = histogram_shader.version_create();
		for (int i = 0; i < HISTOGRAM_MODE_MAX; i++) {
			histogram_pipelines[i] = RD::get_singleton()->compute_pipeline_create(histogram_shader.version_get_shader(histogram_shader_version, i));
		}
	}
	// Always allocated (4 KiB): the finish shader declares it, so every draw binds it, histogram or not.
	histogram_buffer = RD::get_singleton()->storage_buffer_create(HISTOGRAM_WORDS * sizeof(uint32_t));
}

CameraFinish::~CameraFinish() {
	RD::get_singleton()->free_rid(histogram_buffer);
	finish_shader.version_free(finish_shader_version);
	histogram_shader.version_free(histogram_shader_version);
}

void CameraFinish::_draw(RID p_source, RID p_dest_framebuffer, const FinishPushConstant &p_push_constant) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RID shader = finish_shader.version_get_shader(finish_shader_version, 0);
	ERR_FAIL_COND(shader.is_null());

	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_histogram(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, histogram_buffer);

	RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(p_dest_framebuffer, RD::DRAW_IGNORE_ALL);
	RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, finish_pipeline.get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(p_dest_framebuffer)));
	RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader, 0, u_source, u_histogram), 0);
	RD::get_singleton()->draw_list_set_push_constant(draw_list, &p_push_constant, sizeof(FinishPushConstant));
	RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
	RD::get_singleton()->draw_list_end();
}

void CameraFinish::_compute_histogram(RID p_source, Size2i p_size, bool p_linear_input) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RD::get_singleton()->buffer_clear(histogram_buffer, 0, HISTOGRAM_WORDS * sizeof(uint32_t));

	HistogramPushConstant push_constant = {};
	push_constant.size[0] = p_size.x;
	push_constant.size[1] = p_size.y;
	push_constant.linear_input = p_linear_input ? 1 : 0;

	RID count_shader = histogram_shader.version_get_shader(histogram_shader_version, HISTOGRAM_COUNT);
	RID max_shader = histogram_shader.version_get_shader(histogram_shader_version, HISTOGRAM_MAX);
	ERR_FAIL_COND(count_shader.is_null() || max_shader.is_null());

	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_histogram(RD::UNIFORM_TYPE_STORAGE_BUFFER, 1, histogram_buffer);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, histogram_pipelines[HISTOGRAM_COUNT]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(count_shader, 0, u_source, u_histogram), 0);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(HistogramPushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_size.x, p_size.y, 1);
	RD::get_singleton()->compute_list_add_barrier(compute_list);

	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, histogram_pipelines[HISTOGRAM_MAX]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(max_shader, 0, u_histogram), 0);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(HistogramPushConstant));
	RD::get_singleton()->compute_list_dispatch(compute_list, 1, 1, 1);
	RD::get_singleton()->compute_list_end();
}

void CameraFinish::process(Ref<RenderSceneBuffersRD> p_render_buffers, const Settings &p_settings) {
	ERR_FAIL_COND(p_render_buffers.is_null());
	if (!is_active(p_settings) || p_render_buffers->get_view_count() != 1) {
		return;
	}
	TextureStorage *texture_storage = TextureStorage::get_singleton();
	RID render_target = p_render_buffers->get_render_target();
	RID rt_texture = texture_storage->render_target_get_rd_texture(render_target);
	RID rt_framebuffer = texture_storage->render_target_get_rd_framebuffer(render_target);
	ERR_FAIL_COND(rt_texture.is_null() || rt_framebuffer.is_null());

	const Size2i size = p_render_buffers->get_target_size();
	const RD::DataFormat format = RD::get_singleton()->texture_get_format(rt_texture).format;
	const bool linear_input = texture_storage->render_target_is_using_hdr(render_target);

	// Two scratch copies of the render target, ping-ponged by the passes; the last pass writes the render target.
	const StringName context = SNAME("camera_finish");
	if (p_render_buffers->has_texture(context, SNAME("a"))) {
		const RD::TextureFormat scratch_format = RD::get_singleton()->texture_get_format(p_render_buffers->get_texture(context, SNAME("a")));
		if (scratch_format.format != format || int(scratch_format.width) != size.x || int(scratch_format.height) != size.y) {
			p_render_buffers->clear_context(context); // HDR 2D toggled, or the target resized.
		}
	}
	if (!p_render_buffers->has_texture(context, SNAME("a"))) {
		const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		p_render_buffers->create_texture(context, SNAME("a"), format, usage, RD::TEXTURE_SAMPLES_1, size, 1, 1, true);
		p_render_buffers->create_texture(context, SNAME("b"), format, usage, RD::TEXTURE_SAMPLES_1, size, 1, 1, true);
	}
	RID scratch[2] = { p_render_buffers->get_texture(context, SNAME("a")), p_render_buffers->get_texture(context, SNAME("b")) };

	RD::get_singleton()->draw_command_begin_label("Camera Finish");

	RD::get_singleton()->texture_copy(rt_texture, scratch[0], Vector3(), Vector3(), Vector3(size.x, size.y, 1), 0, 0, 0, 0);

	const RendererCameraAttributes::CameraFinishSettings &camera = p_settings.camera;
	const RendererSceneRender::CameraMonitoring &monitoring = p_settings.monitoring;

	FinishPushConstant push_constant = {};
	push_constant.pixel_size[0] = 1.0f / size.x;
	push_constant.pixel_size[1] = 1.0f / size.y;
	push_constant.flags = linear_input ? FLAG_LINEAR_INPUT : 0;
	push_constant.nr_spatial_sigma = MAX(camera.nr_spatial_sigma_px, 0.1f);
	push_constant.nr_range_sigma = MAX(camera.nr_range_sigma, 1e-4f);
	push_constant.sharpen_amount = camera.sharpen_amount;
	push_constant.sharpen_sigma = MAX(camera.sharpen_sigma_px, 0.1f);
	push_constant.sharpen_halo = camera.sharpen_halo;
	if (camera.nr_chroma_only) {
		push_constant.flags |= FLAG_NR_CHROMA_ONLY;
	}

	LocalVector<Mode> passes;
	if (camera.noise_reduction) {
		passes.push_back(MODE_NOISE_REDUCTION);
	}
	if (camera.sharpening) {
		passes.push_back(MODE_SHARPEN);
	}
	if (monitoring.is_active()) {
		passes.push_back(MODE_OVERLAY);
	}

	int current = 0;
	for (uint32_t i = 0; i < passes.size(); i++) {
		const bool last = i == passes.size() - 1;
		RID dest_framebuffer = last ? rt_framebuffer : FramebufferCacheRD::get_singleton()->get_cache(scratch[current ^ 1]);
		push_constant.mode = passes[i];

		if (passes[i] == MODE_OVERLAY) {
			// Zebra stripes are 1/90 of the frame height apart (12 px at 1080p), a look choice close to camera
			// monitors. The levels are IRE percent of the full-range video signal.
			push_constant.time = Math::fmod(p_settings.time, 3600.0);
			push_constant.stripe_period = MAX(size.y / 90.0f, 4.0f);
			push_constant.zebras_level = monitoring.zebras_level * 0.01f;
			push_constant.zebras_second_level = monitoring.zebras_second_level * 0.01f;
			if (monitoring.zebras) {
				push_constant.flags |= FLAG_ZEBRAS;
			}
			if (monitoring.zebras_second) {
				push_constant.flags |= FLAG_ZEBRAS_SECOND;
			}
			// Display-encoded stripe colors, written as they are (as the focus peaking color is).
			for (int c = 0; c < 3; c++) {
				push_constant.zebras_color[c] = monitoring.zebras_color[c];
				push_constant.zebras_second_color[c] = monitoring.zebras_second_color[c];
			}
			push_constant.zebras_color[3] = 1.0f;
			push_constant.zebras_second_color[3] = 1.0f;
			if (monitoring.false_color) {
				push_constant.flags |= FLAG_FALSE_COLOR;
			}
			if (monitoring.focus_peaking) {
				push_constant.flags |= FLAG_FOCUS_PEAKING;
				// Sensitivity 0..1 maps to a Sobel threshold of 0.4..0.03 of full scale per pixel: at 0.5, edges of
				// about a fifth of full scale crossed within a pixel light up. A look choice, tuned on test charts.
				push_constant.peaking_threshold = 0.4f - 0.37f * monitoring.focus_peaking_sensitivity;
				push_constant.peaking_color[0] = monitoring.focus_peaking_color.r;
				push_constant.peaking_color[1] = monitoring.focus_peaking_color.g;
				push_constant.peaking_color[2] = monitoring.focus_peaking_color.b;
				push_constant.peaking_color[3] = 1.0f;
			}
			if (monitoring.histogram) {
				push_constant.flags |= FLAG_HISTOGRAM;
				if (monitoring.histogram_mode == RSE::CAMERA_MONITORING_HISTOGRAM_RGB) {
					push_constant.flags |= FLAG_HISTOGRAM_RGB;
				}
				// The histogram is 2:1, its width a fraction of the frame width, inset from its corner by 3% of the
				// frame height.
				const float width = Math::round(size.x * monitoring.histogram_size);
				const float height = Math::round(MIN(width * 0.5f, size.y * 0.9f));
				const float margin = Math::round(size.y * 0.03f);
				const bool right = monitoring.histogram_position == RSE::CAMERA_MONITORING_HISTOGRAM_TOP_RIGHT || monitoring.histogram_position == RSE::CAMERA_MONITORING_HISTOGRAM_BOTTOM_RIGHT;
				const bool bottom = monitoring.histogram_position == RSE::CAMERA_MONITORING_HISTOGRAM_BOTTOM_LEFT || monitoring.histogram_position == RSE::CAMERA_MONITORING_HISTOGRAM_BOTTOM_RIGHT;
				const float x0 = right ? MAX(size.x - margin - width, 0.0f) : margin;
				const float y0 = bottom ? MAX(size.y - margin - height, 0.0f) : margin;
				push_constant.histogram_rect[0] = x0;
				push_constant.histogram_rect[1] = y0;
				push_constant.histogram_rect[2] = MIN(x0 + width, float(size.x));
				push_constant.histogram_rect[3] = MIN(y0 + height, float(size.y));
				push_constant.histogram_opacity = monitoring.histogram_opacity;

				// Counted on the image as it is before the overlays.
				_compute_histogram(scratch[current], size, linear_input);
			}
		}

		_draw(scratch[current], dest_framebuffer, push_constant);
		current ^= 1;
	}

	RD::get_singleton()->draw_command_end_label();
}
