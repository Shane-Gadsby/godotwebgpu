/**************************************************************************/
/*  camera_local_exposure.cpp                                             */
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

#include "camera_local_exposure.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

CameraLocalExposure::CameraLocalExposure() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_BUILD\n"); // MODE_BUILD
	modes.push_back("\n#define MODE_BLUR\n"); // MODE_BLUR
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

CameraLocalExposure::~CameraLocalExposure() {
	shader.version_free(shader_version);
}

void CameraLocalExposure::LocalExposureBuffers::free_data() {
	if (raw_grid.is_valid()) {
		RD::get_singleton()->free_rid(raw_grid);
		raw_grid = RID();
	}
	if (grid.is_valid()) {
		RD::get_singleton()->free_rid(grid);
		grid = RID();
	}
	grid_size = Size2i();
}

Ref<CameraLocalExposure::LocalExposureBuffers> CameraLocalExposure::get_buffers(Ref<RenderSceneBuffersRD> p_render_buffers) {
	if (p_render_buffers->has_custom_data(RB_CAMERA_LOCAL_EXPOSURE_BUFFERS)) {
		return p_render_buffers->get_custom_data(RB_CAMERA_LOCAL_EXPOSURE_BUFFERS);
	}
	Ref<LocalExposureBuffers> buffers;
	buffers.instantiate();
	p_render_buffers->set_custom_data(RB_CAMERA_LOCAL_EXPOSURE_BUFFERS, buffers);
	return buffers;
}

RID CameraLocalExposure::build_grid(RID p_source, const Size2i &p_source_size, Ref<LocalExposureBuffers> p_buffers, const Settings &p_settings) {
	ERR_FAIL_COND_V(p_buffers.is_null(), RID());
	const Size2i grid_size = p_settings.local_exposure.grid_size;
	ERR_FAIL_COND_V(grid_size.x <= 0 || grid_size.y <= 0, RID());
	const int grid_depth = RendererCameraAttributes::LOCAL_EXPOSURE_GRID_DEPTH + 1; // The last slice: blurred luminance.

	// Sized by the frame's aspect ratio, which the texture the tonemap pass reads can change (an upscaler's output).
	if (p_buffers->grid_size != grid_size) {
		p_buffers->free_data();
		RD::TextureFormat tf;
		tf.texture_type = RD::TEXTURE_TYPE_3D;
		tf.width = grid_size.x;
		tf.height = grid_size.y;
		tf.depth = grid_depth;
		tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT;
		p_buffers->raw_grid = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(p_buffers->raw_grid, "Local exposure raw grid");
		tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
		p_buffers->grid = RD::get_singleton()->texture_create(tf, RD::TextureView());
		RD::get_singleton()->set_resource_name(p_buffers->grid, "Local exposure grid");
		p_buffers->grid_size = grid_size;
	}

	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	RID sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	PushConstant push_constant = {};
	push_constant.source_size[0] = p_source_size.x;
	push_constant.source_size[1] = p_source_size.y;
	push_constant.grid_size[0] = grid_size.x;
	push_constant.grid_size[1] = grid_size.y;
	push_constant.cell_size = p_settings.local_exposure.cell_size;
	push_constant.exposure = p_settings.exposure;
	push_constant.auto_exposure_scale = p_settings.auto_exposure_scale;
	push_constant.luminance_multiplier = p_settings.luminance_multiplier;
	push_constant.blur_sigma = MAX(p_settings.local_exposure.blurred_luminance_sigma, 0.05f);
	// +-3 sigma; past that the Gaussian is below 1.1% of its peak. Capped at the grid, beyond which every tap is empty.
	push_constant.blur_radius = MIN(int(Math::ceil(3.0f * push_constant.blur_sigma)), MAX(grid_size.x, grid_size.y));
	if (p_settings.use_auto_exposure && p_settings.auto_exposure_texture.is_valid()) {
		push_constant.flags |= FLAG_USE_AUTO_EXPOSURE;
	}
	RID exposure_texture = (push_constant.flags & FLAG_USE_AUTO_EXPOSURE) ? p_settings.auto_exposure_texture : TextureStorage::get_singleton()->texture_rd_get_default(TextureStorage::DEFAULT_RD_TEXTURE_WHITE);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();

	RID build_shader = shader.version_get_shader(shader_version, MODE_BUILD);
	RD::Uniform u_source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ sampler, p_source }));
	RD::Uniform u_exposure(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ sampler, exposure_texture }));
	RD::Uniform u_raw_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->raw_grid);
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_BUILD]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(build_shader, 0, u_source, u_exposure), 0);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(build_shader, 1, u_raw_dest), 1);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch(compute_list, grid_size.x, grid_size.y, 1); // One workgroup per cell column.

	RD::get_singleton()->compute_list_add_barrier(compute_list);

	RID blur_shader = shader.version_get_shader(shader_version, MODE_BLUR);
	RD::Uniform u_raw_source(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->raw_grid);
	RD::Uniform u_grid_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_buffers->grid);
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_BLUR]);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(blur_shader, 0, u_raw_source), 0);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(blur_shader, 1, u_grid_dest), 1);
	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, grid_size.x, grid_size.y, grid_depth);

	RD::get_singleton()->compute_list_end();

	return p_buffers->grid;
}
