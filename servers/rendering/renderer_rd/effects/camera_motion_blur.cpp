/**************************************************************************/
/*  camera_motion_blur.cpp                                                */
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

#include "camera_motion_blur.h"

#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

CameraMotionBlur::CameraMotionBlur() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_PREP\n");
	modes.push_back("\n#define MODE_PREP_RESOLVED_DEPTH\n");
	modes.push_back("\n#define MODE_TILE_MAX\n");
	modes.push_back("\n#define MODE_NEIGHBOR_MAX\n");
	modes.push_back("\n#define MODE_GATHER\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i] = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, i));
	}
}

CameraMotionBlur::~CameraMotionBlur() {
	shader.version_free(shader_version);
}

void CameraMotionBlur::process(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, const Parameters &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	ERR_FAIL_COND(p_params.color_size.x <= 0 || p_params.color_size.y <= 0);

	// The filter needs tiles at least as large as the longest blur radius, so a pixel's 3x3 tile neighborhood holds
	// every blur that can reach it. The radius follows the frame height, so the tiles do too.
	const float max_radius = MAX(p_params.max_length * p_params.color_size.y * 0.5f, 1.0f);
	const int tile_size = (int)Math::ceil(max_radius);
	const Size2i tile_count = Size2i((p_params.color_size.x + tile_size - 1) / tile_size, (p_params.color_size.y + tile_size - 1) / tile_size);

	const StringName context = SNAME("camera_motion_blur");
	if (p_render_buffers->has_texture(context, SNAME("prep"))) {
		const RD::TextureFormat prep_format = p_render_buffers->get_texture_format(context, SNAME("prep"));
		const RD::TextureFormat tile_format = p_render_buffers->get_texture_format(context, SNAME("tiles"));
		if ((int)prep_format.width != p_params.color_size.x || (int)prep_format.height != p_params.color_size.y || (int)tile_format.width != tile_count.x || (int)tile_format.height != tile_count.y) {
			p_render_buffers->clear_context(context);
		}
	}
	if (!p_render_buffers->has_texture(context, SNAME("prep"))) {
		const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		// RGBA16F throughout: WebGPU allows no two-channel 16-bit storage textures.
		p_render_buffers->create_texture(context, SNAME("prep"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, p_params.color_size);
		p_render_buffers->create_texture(context, SNAME("tiles"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, tile_count);
		p_render_buffers->create_texture(context, SNAME("neighbors"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, tile_count);
		p_render_buffers->create_texture(context, SNAME("output"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT, RD::TEXTURE_SAMPLES_1, p_params.color_size);
	}
	RID prep = p_render_buffers->get_texture_slice(context, SNAME("prep"), p_view, 0);
	RID tiles = p_render_buffers->get_texture_slice(context, SNAME("tiles"), p_view, 0);
	RID neighbors = p_render_buffers->get_texture_slice(context, SNAME("neighbors"), p_view, 0);
	RID output = p_render_buffers->get_texture_slice(context, SNAME("output"), p_view, 0);

	PushConstant push_constant;
	memset(&push_constant, 0, sizeof(PushConstant));
	MaterialStorage::store_camera(p_params.reprojection, push_constant.reprojection);
	push_constant.size[0] = p_params.color_size.x;
	push_constant.size[1] = p_params.color_size.y;
	push_constant.tile_count[0] = tile_count.x;
	push_constant.tile_count[1] = tile_count.y;
	push_constant.blur_scale = p_params.blur_scale;
	push_constant.max_radius = max_radius;
	push_constant.rolling_shutter = p_params.rolling_shutter;
	push_constant.z_near = p_params.z_near;
	push_constant.z_far = p_params.z_far;
	push_constant.tile_size = tile_size;
	push_constant.sample_count = p_params.sample_count;
	push_constant.frame = p_params.frame;
	push_constant.orthogonal = p_params.orthogonal;

	RID nearest_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID linear_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RD::get_singleton()->draw_command_begin_label("Camera Motion Blur");
	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();

	{
		const Mode mode = p_params.depth_is_resolved ? MODE_PREP_RESOLVED_DEPTH : MODE_PREP;
		RID shader_rid = shader.version_get_shader(shader_version, mode);
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest_sampler, p_params.depth }));
		RD::Uniform u_velocity(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, p_params.velocity }));
		RD::Uniform u_prep(RD::UNIFORM_TYPE_IMAGE, 2, prep);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[mode]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rid, 0, u_depth, u_velocity, u_prep), 0);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_params.color_size.x, p_params.color_size.y, 1);
		RD::get_singleton()->compute_list_add_barrier(compute_list);
	}

	if (p_params.blur_scale > 0.0f) {
		{
			RID shader_rid = shader.version_get_shader(shader_version, MODE_TILE_MAX);
			RD::Uniform u_prep(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest_sampler, prep }));
			RD::Uniform u_tiles(RD::UNIFORM_TYPE_IMAGE, 1, tiles);
			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_TILE_MAX]);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rid, 0, u_prep, u_tiles), 0);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
			RD::get_singleton()->compute_list_dispatch(compute_list, tile_count.x, tile_count.y, 1); // One workgroup per tile.
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}
		{
			RID shader_rid = shader.version_get_shader(shader_version, MODE_NEIGHBOR_MAX);
			RD::Uniform u_tiles(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest_sampler, tiles }));
			RD::Uniform u_neighbors(RD::UNIFORM_TYPE_IMAGE, 1, neighbors);
			RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_NEIGHBOR_MAX]);
			RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rid, 0, u_tiles, u_neighbors), 0);
			RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
			RD::get_singleton()->compute_list_dispatch_threads(compute_list, tile_count.x, tile_count.y, 1);
			RD::get_singleton()->compute_list_add_barrier(compute_list);
		}
	}

	{
		RID shader_rid = shader.version_get_shader(shader_version, MODE_GATHER);
		RD::Uniform u_color(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, p_params.color }));
		RD::Uniform u_prep(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, prep }));
		// Without blur the gather does not read the neighborhood, but the binding must still hold a texture.
		RD::Uniform u_neighbors(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest_sampler, neighbors }));
		RD::Uniform u_output(RD::UNIFORM_TYPE_IMAGE, 3, output);
		RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipelines[MODE_GATHER]);
		RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader_rid, 0, u_color, u_prep, u_neighbors, u_output), 0);
		RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_params.color_size.x, p_params.color_size.y, 1);
	}

	RD::get_singleton()->compute_list_end();

	CopyEffects::get_singleton()->copy_to_rect(output, p_params.color, Rect2i(Point2i(), p_params.color_size));

	RD::get_singleton()->draw_command_end_label();
}
