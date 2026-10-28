/**************************************************************************/
/*  light_shafts.cpp                                                      */
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

#include "light_shafts.h"

#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

static constexpr float LIGHT_SHAFTS_CUTOFF = 6.907755f; // ln(1000); see RendererEnvironmentStorage::LightShafts.

LightShafts::LightShafts() {
	Vector<String> modes;
	modes.push_back("\n#define MODE_MASK\n");
	modes.push_back("\n#define MODE_MASK_RESOLVED_DEPTH\n");
	modes.push_back("\n#define MODE_BLUR\n");
	modes.push_back("\n#define MODE_COMPOSITE\n");
	shader.initialize(modes);
	shader_version = shader.version_create();

	RD::PipelineColorBlendState additive = RD::PipelineColorBlendState::create_disabled();
	additive.attachments.write[0].enable_blend = true;
	additive.attachments.write[0].src_color_blend_factor = RD::BLEND_FACTOR_ONE;
	additive.attachments.write[0].dst_color_blend_factor = RD::BLEND_FACTOR_ONE;
	additive.attachments.write[0].src_alpha_blend_factor = RD::BLEND_FACTOR_ZERO;
	additive.attachments.write[0].dst_alpha_blend_factor = RD::BLEND_FACTOR_ONE;
	additive.attachments.write[0].write_a = false;

	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i].setup(shader.version_get_shader(shader_version, i), RD::RENDER_PRIMITIVE_TRIANGLES, RD::PipelineRasterizationState(), RD::PipelineMultisampleState(), RD::PipelineDepthStencilState(), i == MODE_COMPOSITE ? additive : RD::PipelineColorBlendState::create_disabled(), 0);
	}
}

LightShafts::~LightShafts() {
	for (int i = 0; i < MODE_MAX; i++) {
		pipelines[i].clear();
	}
	shader.version_free(shader_version);
}

void LightShafts::process(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, const Parameters &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	ERR_FAIL_COND(p_params.color_size.x <= 0 || p_params.color_size.y <= 0);

	// A quarter of the width and height, as Mitchell's (and Unreal's) shaft buffers: the shafts are soft, and the blur
	// is the pass that costs.
	const Size2i size = Size2i(MAX(p_params.color_size.x / 4, 1), MAX(p_params.color_size.y / 4, 1));

	const StringName context = SNAME("light_shafts");
	if (p_render_buffers->has_texture(context, SNAME("mask"))) {
		const RD::TextureFormat format = p_render_buffers->get_texture_format(context, SNAME("mask"));
		if ((int)format.width != size.x || (int)format.height != size.y) {
			p_render_buffers->clear_context(context);
		}
	}
	if (!p_render_buffers->has_texture(context, SNAME("mask"))) {
		const uint32_t usage = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		p_render_buffers->create_texture(context, SNAME("mask"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, size);
		p_render_buffers->create_texture(context, SNAME("shafts"), RD::DATA_FORMAT_R16G16B16A16_SFLOAT, usage, RD::TEXTURE_SAMPLES_1, size);
	}
	RID mask = p_render_buffers->get_texture_slice(context, SNAME("mask"), p_view, 0);
	RID shafts = p_render_buffers->get_texture_slice(context, SNAME("shafts"), p_view, 0);

	PushConstant push_constant;
	memset(&push_constant, 0, sizeof(PushConstant));
	for (int i = 0; i < 4; i++) {
		push_constant.sun[i] = p_params.sun.position[i];
	}
	const float scale = p_params.intensity * p_params.sun.fade * p_params.luminance_multiplier;
	push_constant.scale[0] = p_params.tint.r * scale;
	push_constant.scale[1] = p_params.tint.g * scale;
	push_constant.scale[2] = p_params.tint.b * scale;
	push_constant.threshold = p_params.threshold;
	push_constant.decay = MAX(p_params.decay, 0.01f);
	push_constant.max_length = LIGHT_SHAFTS_CUTOFF / push_constant.decay;
	push_constant.luminance_multiplier = p_params.luminance_multiplier;
	push_constant.sample_count = MAX(p_params.sample_count, 1u);

	RID nearest_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID linear_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	RD::get_singleton()->draw_command_begin_label("Light Shafts");

	{
		const Mode mode = p_params.depth_is_resolved ? MODE_MASK_RESOLVED_DEPTH : MODE_MASK;
		RID shader_rid = shader.version_get_shader(shader_version, mode);
		RID fb = FramebufferCacheRD::get_singleton()->get_cache(mask);
		RD::Uniform u_color(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, p_params.color }));
		RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest_sampler, p_params.depth }));
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(fb, RD::DRAW_IGNORE_COLOR_0);
		RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipelines[mode].get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(fb)));
		RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader_rid, 0, u_color, u_depth), 0);
		RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
		RD::get_singleton()->draw_list_end();
	}

	{
		RID shader_rid = shader.version_get_shader(shader_version, MODE_BLUR);
		RID fb = FramebufferCacheRD::get_singleton()->get_cache(shafts);
		RD::Uniform u_mask(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, mask }));
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(fb, RD::DRAW_IGNORE_COLOR_0);
		RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipelines[MODE_BLUR].get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(fb)));
		RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader_rid, 0, u_mask), 0);
		RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
		RD::get_singleton()->draw_list_end();
	}

	{
		RID shader_rid = shader.version_get_shader(shader_version, MODE_COMPOSITE);
		RID fb = FramebufferCacheRD::get_singleton()->get_cache(p_params.color);
		RD::Uniform u_shafts(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ linear_sampler, shafts }));
		RD::DrawListID draw_list = RD::get_singleton()->draw_list_begin(fb);
		RD::get_singleton()->draw_list_bind_render_pipeline(draw_list, pipelines[MODE_COMPOSITE].get_render_pipeline(RD::INVALID_ID, RD::get_singleton()->framebuffer_get_format(fb)));
		RD::get_singleton()->draw_list_bind_uniform_set(draw_list, uniform_set_cache->get_cache(shader_rid, 0, u_shafts), 0);
		RD::get_singleton()->draw_list_set_push_constant(draw_list, &push_constant, sizeof(PushConstant));
		RD::get_singleton()->draw_list_draw(draw_list, false, 1u, 3u);
		RD::get_singleton()->draw_list_end();
	}

	RD::get_singleton()->draw_command_end_label();
}
