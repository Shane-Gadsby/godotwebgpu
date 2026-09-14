/**************************************************************************/
/*  fsr.cpp                                                               */
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

#include "fsr.h"

#include "servers/rendering/renderer_rd/effects/copy_effects.h"
#include "servers/rendering/renderer_rd/framebuffer_cache_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

FSR::FSR() {
	bool has_half_float = RD::get_singleton()->has_feature(RD::SUPPORTS_HALF_FLOAT);

	Vector<String> fsr_upscale_modes;
	// ShaderRD compiles every registered mode eagerly (not just the one actually
	// selected below), so the FSR_SHADER_VARIANT_NORMAL entry must still produce
	// valid, compilable code on backends without half-float support -- it's
	// dead code there (never selected), but still needs to exist. MODE_FSR_UPSCALE_NORMAL's
	// A_HALF path (thirdparty/amd-fsr/ffx_a.h) uses GLSL's 16-bit int/uint types
	// (int16_t/uint16_t and vector forms) for its packed-precision optimization --
	// WGSL only has 32-bit integers, so Tint's SPIR-V reader hard-crashes
	// (TINT_ASSERT(int_ty->width() == 32)) on any backend that never actually
	// executes this path but still has to compile it. So: when half-float
	// support is absent, register the FALLBACK defines under both slots instead
	// (harmless duplication of already-valid code; the NORMAL slot is provably
	// unreachable at runtime either way, see below) rather than ever asking any
	// backend without half-float support to compile the 16-bit-int path at all.
	// See webgpu_notes/TASKS.md Task 9.5.
	fsr_upscale_modes.push_back(has_half_float ? "\n#define MODE_FSR_UPSCALE_NORMAL\n" : "\n#define MODE_FSR_UPSCALE_FALLBACK\n");
	fsr_upscale_modes.push_back("\n#define MODE_FSR_UPSCALE_FALLBACK\n");
	fsr_shader.initialize(fsr_upscale_modes);

	FSRShaderVariant variant;
	if (has_half_float) {
		variant = FSR_SHADER_VARIANT_NORMAL;
	} else {
		variant = FSR_SHADER_VARIANT_FALLBACK;
	}

	shader_version = fsr_shader.version_create();
	pipeline.create_compute_pipeline(fsr_shader.version_get_shader(shader_version, variant));
}

FSR::~FSR() {
	pipeline.free();
	fsr_shader.version_free(shader_version);
}

void FSR::process(Ref<RenderSceneBuffersRD> p_render_buffers, RID p_source_rd_texture, RID p_destination_texture) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);

	Size2i internal_size = p_render_buffers->get_internal_size();
	Size2i target_size = p_render_buffers->get_target_size();
	float fsr_upscale_sharpness = p_render_buffers->get_fsr_sharpness();

	if (!p_render_buffers->has_texture(SNAME("FSR"), SNAME("upscale_texture"))) {
		RD::DataFormat format = p_render_buffers->get_base_data_format();
		uint32_t usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		uint32_t layers = 1; // we only need one layer, in multiview we're processing one layer at a time.

		p_render_buffers->create_texture(SNAME("FSR"), SNAME("upscale_texture"), format, usage_bits, RD::TEXTURE_SAMPLES_1, target_size, layers);
	}

	RID upscale_texture = p_render_buffers->get_texture(SNAME("FSR"), SNAME("upscale_texture"));

	// RCAS writes its final result via a compute-shader imageStore -- normally
	// straight into p_destination_texture (the viewport's render target color
	// texture), same as native backends. WebGPU can't do that: this fork
	// deliberately omits TEXTURE_USAGE_STORAGE_BIT from that texture there
	// (texture_storage.cpp's render_target_get_color_usage_bits() -- Dawn
	// rejects StorageBinding combined with the sRGB view sharing that texture
	// also needs). Detect that at runtime (rather than assuming it's WebGPU
	// specifically -- any backend that ever can't write directly here hits the
	// same fallback) and, when direct storage-write isn't available, route
	// RCAS's output through an intermediate texture instead, then blit it into
	// the real destination with CopyEffects -- the same "write to an internal
	// buffer, never compute-write the render target directly" pattern
	// FSR2/MetalFX Temporal already use for an unrelated reason. The
	// intermediate MUST be p_render_buffers->get_base_data_format()
	// (RGBA16_SFLOAT), matching upscale_texture, not p_destination_texture's
	// own format: fsr_upscale.glsl's `fsr_image` storage-image binding is
	// hardcoded `layout(rgba16f, ...)` (shared verbatim between the EASU and
	// RCAS passes), so it can only ever be bound to an RGBA16F view regardless
	// of what format the final destination actually needs to end up in -- a
	// raw GPU texture_copy() can't bridge that gap (it requires identical
	// formats on both sides), so this uses a real shader blit (CopyEffects),
	// which converts between mismatched formats as a normal part of
	// rasterizing into a color attachment. See webgpu_notes/TASKS.md's FSR1 task.
	bool dest_supports_storage = (RD::get_singleton()->texture_get_format(p_destination_texture).usage_bits & RD::TEXTURE_USAGE_STORAGE_BIT) != 0;
	RID rcas_output_texture = p_destination_texture;
	if (!dest_supports_storage) {
		if (!p_render_buffers->has_texture(SNAME("FSR"), SNAME("rcas_output"))) {
			RD::DataFormat format = p_render_buffers->get_base_data_format();
			uint32_t usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
			p_render_buffers->create_texture(SNAME("FSR"), SNAME("rcas_output"), format, usage_bits, RD::TEXTURE_SAMPLES_1, target_size, 1);
		}
		rcas_output_texture = p_render_buffers->get_texture(SNAME("FSR"), SNAME("rcas_output"));
	}

	FSRUpscalePushConstant push_constant;
	memset(&push_constant, 0, sizeof(FSRUpscalePushConstant));

	int dispatch_x = (target_size.x + 15) / 16;
	int dispatch_y = (target_size.y + 15) / 16;

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline.get_rid());

	push_constant.resolution_width = internal_size.width;
	push_constant.resolution_height = internal_size.height;
	push_constant.upscaled_width = target_size.width;
	push_constant.upscaled_height = target_size.height;
	push_constant.sharpness = fsr_upscale_sharpness;

	RID shader = fsr_shader.version_get_shader(shader_version, 0);
	ERR_FAIL_COND(shader.is_null());

	RID default_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	//FSR Easc
	RD::Uniform u_source_rd_texture(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, { default_sampler, p_source_rd_texture });
	RD::Uniform u_upscale_texture(RD::UNIFORM_TYPE_IMAGE, 0, { upscale_texture });

	push_constant.pass = FSR_UPSCALE_PASS_EASU;
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_source_rd_texture), 0);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_upscale_texture), 1);

	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(FSRUpscalePushConstant));

	RD::get_singleton()->compute_list_dispatch(compute_list, dispatch_x, dispatch_y, 1);
	RD::get_singleton()->compute_list_add_barrier(compute_list);

	//FSR Rcas
	RD::Uniform u_upscale_texture_with_sampler(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, { default_sampler, upscale_texture });
	RD::Uniform u_destination_texture(RD::UNIFORM_TYPE_IMAGE, 0, { rcas_output_texture });

	push_constant.pass = FSR_UPSCALE_PASS_RCAS;
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 0, u_upscale_texture_with_sampler), 0);
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set_cache->get_cache(shader, 1, u_destination_texture), 1);

	RD::get_singleton()->compute_list_set_push_constant(compute_list, &push_constant, sizeof(FSRUpscalePushConstant));

	RD::get_singleton()->compute_list_dispatch(compute_list, dispatch_x, dispatch_y, 1);

	RD::get_singleton()->compute_list_end();

	if (!dest_supports_storage) {
		RendererRD::CopyEffects *copy_effects = RendererRD::CopyEffects::get_singleton();
		ERR_FAIL_NULL(copy_effects);
		RID dest_fb = FramebufferCacheRD::get_singleton()->get_cache(p_destination_texture);
		copy_effects->copy_to_fb_rect(rcas_output_texture, dest_fb, Rect2i(Point2i(), target_size), false, false, false, false, RID(), false, false, false, false, Rect2(), 1.0, false);
	}
}
