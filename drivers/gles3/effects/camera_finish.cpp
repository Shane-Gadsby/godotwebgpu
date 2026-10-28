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

#ifdef GLES3_ENABLED

#include "drivers/gles3/storage/texture_storage.h"
#include "drivers/gles3/storage/utilities.h"

using namespace GLES3;

CameraFinish::CameraFinish() {
	shader.initialize();
	shader_version = shader.version_create();

	glGenBuffers(1, &screen_triangle);
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
	const float qv[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
	glGenVertexArrays(1, &screen_triangle_array);
	glBindVertexArray(screen_triangle_array);
	glVertexAttribPointer(RSE::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
	glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

CameraFinish::~CameraFinish() {
	_free_scratch();
	for (KeyValue<RID, CameraCleanFrame> &E : camera_clean_frames) {
		_free_clean_frame(E.value);
	}
	glDeleteBuffers(1, &screen_triangle);
	glDeleteVertexArrays(1, &screen_triangle_array);
	shader.version_free(shader_version);
}

void CameraFinish::_free_scratch() {
	if (scratch_fbos[0] != 0) {
		glDeleteFramebuffers(2, scratch_fbos);
		glDeleteTextures(2, scratch_textures);
	}
	scratch_fbos[0] = scratch_fbos[1] = 0;
	scratch_textures[0] = scratch_textures[1] = 0;
	scratch_size = Size2i();
}

void CameraFinish::_free_clean_frame(CameraCleanFrame &p_clean) {
	if (p_clean.fbo != 0) {
		glDeleteFramebuffers(1, &p_clean.fbo);
		p_clean.fbo = 0;
	}
	if (p_clean.tex != 0) {
		GLES3::Utilities::get_singleton()->texture_free_data(p_clean.tex);
		p_clean.tex = 0;
	}
}

void CameraFinish::free_camera_clean_frame(RID p_camera) {
	CameraCleanFrame *clean = camera_clean_frames.getptr(p_camera);
	if (clean) {
		// The server texture still names the GL texture; it is its owner's (Camera3D's) to free, right after this.
		_free_clean_frame(*clean);
		camera_clean_frames.erase(p_camera);
	}
}

void CameraFinish::_copy_clean_frame(const RendererSceneRender::CameraMonitoring &p_monitoring, GLuint p_source_fbo, RID p_render_target) {
	TextureStorage *texture_storage = TextureStorage::get_singleton();
	if (!p_monitoring.camera.is_valid() || !texture_storage->owns_texture(p_monitoring.clean_texture)) {
		return;
	}
	RenderTarget *rt = texture_storage->get_render_target(p_render_target);
	ERR_FAIL_NULL(rt);
	const Size2i size = rt->size;

	CameraCleanFrame *clean = camera_clean_frames.getptr(p_monitoring.camera);
	if (!clean) {
		clean = &camera_clean_frames.insert(p_monitoring.camera, CameraCleanFrame())->value;
	}
	if (clean->size != size || clean->internal_format != rt->color_internal_format || clean->render_target != p_render_target || clean->texture != p_monitoring.clean_texture || clean->tex == 0) {
		// First frame, a resize, HDR toggled, or another viewport: a new copy target with the render target's format.
		GLuint tex = 0;
		GLuint fbo = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, rt->color_internal_format, size.x, size.y, 0, rt->color_format, rt->color_type, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glBindTexture(GL_TEXTURE_2D, 0);
		GLES3::Utilities::get_singleton()->texture_allocated_data(tex, size.x * size.y * rt->color_format_size, "Camera monitoring clean frame");
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		texture_storage->render_target_copy_texture_set(p_monitoring.clean_texture, tex, p_render_target);
		_free_clean_frame(*clean);
		clean->tex = tex;
		clean->fbo = fbo;
		clean->texture = p_monitoring.clean_texture;
		clean->render_target = p_render_target;
		clean->size = size;
		clean->internal_format = rt->color_internal_format;
	}
	glDisable(GL_SCISSOR_TEST); // Blits are scissored.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, p_source_fbo);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, clean->fbo);
	glBlitFramebuffer(0, 0, size.x, size.y, 0, 0, size.x, size.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

void CameraFinish::process(RID p_render_target, const Settings &p_settings) {
	if (!is_active(p_settings)) {
		return;
	}
	TextureStorage *texture_storage = TextureStorage::get_singleton();
	RenderTarget *rt = texture_storage->get_render_target(p_render_target);
	ERR_FAIL_NULL(rt);
	if (rt->view_count > 1 || rt->fbo == 0) {
		return; // Single view only, as in the RD renderers.
	}
	const Size2i size = rt->size;
	const bool linear_input = texture_storage->render_target_is_using_hdr(p_render_target);

	const RendererCameraAttributes::CameraFinishSettings &camera = p_settings.camera;
	const RendererSceneRender::CameraMonitoring &monitoring = p_settings.monitoring;

	LocalVector<CameraFinishShaderGLES3::ShaderVariant> passes;
	if (camera.noise_reduction) {
		passes.push_back(CameraFinishShaderGLES3::MODE_NOISE_REDUCTION);
	}
	if (camera.sharpening) {
		passes.push_back(CameraFinishShaderGLES3::MODE_SHARPEN);
	}
	const bool overlay = monitoring.zebras || monitoring.zebras_second || monitoring.focus_peaking || monitoring.false_color;
	if (overlay) {
		passes.push_back(CameraFinishShaderGLES3::MODE_OVERLAY);
	}
	const bool clean_frame = monitoring.clean_texture.is_valid();
	if (passes.is_empty()) {
		// Only the clean frame: the render target is it, no scratch copies.
		if (clean_frame) {
			_copy_clean_frame(monitoring, rt->fbo, p_render_target);
			glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
		}
		return;
	}

	if (scratch_size != size || scratch_internal_format != rt->color_internal_format) {
		_free_scratch();
		glGenTextures(2, scratch_textures);
		glGenFramebuffers(2, scratch_fbos);
		for (int i = 0; i < 2; i++) {
			glBindTexture(GL_TEXTURE_2D, scratch_textures[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, rt->color_internal_format, size.x, size.y, 0, rt->color_format, rt->color_type, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			glBindFramebuffer(GL_FRAMEBUFFER, scratch_fbos[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scratch_textures[i], 0);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		scratch_size = size;
		scratch_internal_format = rt->color_internal_format;
	}

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);

	// Copy the render target into the first scratch texture.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, rt->fbo);
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratch_fbos[0]);
	glBlitFramebuffer(0, 0, size.x, size.y, 0, 0, size.x, size.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	int flags = 0;
	if (camera.nr_chroma_only) {
		flags |= 2;
	}
	if (monitoring.zebras) {
		flags |= 4;
	}
	if (monitoring.zebras_second) {
		flags |= 8;
	}
	if (monitoring.focus_peaking) {
		flags |= 16;
	}
	if (monitoring.false_color) {
		flags |= 32;
	}
	const uint64_t specialization = linear_input ? uint64_t(CameraFinishShaderGLES3::LINEAR_INPUT) : uint64_t(0);

	int current = 0;
	for (uint32_t i = 0; i < passes.size(); i++) {
		const CameraFinishShaderGLES3::ShaderVariant mode = passes[i];
		const bool last = i == passes.size() - 1;
		if (clean_frame && mode == CameraFinishShaderGLES3::MODE_OVERLAY) {
			// The overlays are the last pass: what it reads is the finished image without them.
			_copy_clean_frame(monitoring, scratch_fbos[current], p_render_target);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, last ? rt->fbo : scratch_fbos[current ^ 1]);
		glViewport(0, 0, size.x, size.y);

		if (!shader.version_bind_shader(shader_version, mode, specialization)) {
			break;
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, scratch_textures[current]);

		// The same parameters as RendererRD::CameraFinish::process(), see there for their sources.
		shader.version_set_uniform(CameraFinishShaderGLES3::PIXEL_SIZE, 1.0f / size.x, 1.0f / size.y, shader_version, mode, specialization);
		shader.version_set_uniform(CameraFinishShaderGLES3::FLAGS, int32_t(flags), shader_version, mode, specialization);
		if (mode == CameraFinishShaderGLES3::MODE_NOISE_REDUCTION) {
			shader.version_set_uniform(CameraFinishShaderGLES3::NR_SPATIAL_SIGMA, MAX(camera.nr_spatial_sigma_px, 0.1f), shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::NR_RANGE_SIGMA, MAX(camera.nr_range_sigma, 1e-4f), shader_version, mode, specialization);
		} else if (mode == CameraFinishShaderGLES3::MODE_SHARPEN) {
			shader.version_set_uniform(CameraFinishShaderGLES3::SHARPEN_AMOUNT, camera.sharpen_amount, shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::SHARPEN_SIGMA, MAX(camera.sharpen_sigma_px, 0.1f), shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::SHARPEN_HALO, camera.sharpen_halo, shader_version, mode, specialization);
		} else {
			shader.version_set_uniform(CameraFinishShaderGLES3::TIME, float(Math::fmod(p_settings.time, 3600.0)), shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::STRIPE_PERIOD, MAX(size.y / 90.0f, 4.0f), shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::ZEBRAS_LEVEL, monitoring.zebras_level * 0.01f, shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::ZEBRAS_SECOND_LEVEL, monitoring.zebras_second_level * 0.01f, shader_version, mode, specialization);
			shader.version_set_uniform(CameraFinishShaderGLES3::PEAKING_THRESHOLD, 0.4f - 0.37f * monitoring.focus_peaking_sensitivity, shader_version, mode, specialization);
			const Color &c = monitoring.focus_peaking_color;
			shader.version_set_uniform(CameraFinishShaderGLES3::PEAKING_COLOR, Vector3(c.r, c.g, c.b), shader_version, mode, specialization);
			const Color &zc = monitoring.zebras_color;
			shader.version_set_uniform(CameraFinishShaderGLES3::ZEBRAS_COLOR, Vector3(zc.r, zc.g, zc.b), shader_version, mode, specialization);
			const Color &zc2 = monitoring.zebras_second_color;
			shader.version_set_uniform(CameraFinishShaderGLES3::ZEBRAS_SECOND_COLOR, Vector3(zc2.r, zc2.g, zc2.b), shader_version, mode, specialization);
		}

		glBindVertexArray(screen_triangle_array);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glBindVertexArray(0);
		current ^= 1;
	}

	if (clean_frame && !overlay) {
		_copy_clean_frame(monitoring, rt->fbo, p_render_target); // After noise reduction and sharpening.
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
}

#endif // GLES3_ENABLED
