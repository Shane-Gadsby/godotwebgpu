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

#ifdef GLES3_ENABLED

#include "drivers/gles3/storage/config.h"
#include "drivers/gles3/storage/texture_storage.h"
#include "drivers/gles3/storage/utilities.h"

using namespace GLES3;

static constexpr float LIGHT_SHAFTS_CUTOFF = 6.907755f; // ln(1000); see RendererEnvironmentStorage::LightShafts.

LightShafts::LightShafts() {
	shader.initialize();
	shader_version = shader.version_create();

	glGenBuffers(1, &screen_triangle);
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
	const float qv[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6, qv, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenVertexArrays(1, &screen_triangle_array);
	glBindVertexArray(screen_triangle_array);
	glBindBuffer(GL_ARRAY_BUFFER, screen_triangle);
	glVertexAttribPointer(RSE::ARRAY_VERTEX, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
	glEnableVertexAttribArray(RSE::ARRAY_VERTEX);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

LightShafts::~LightShafts() {
	_free_buffers();
	glDeleteBuffers(1, &screen_triangle);
	glDeleteVertexArrays(1, &screen_triangle_array);
	shader.version_free(shader_version);
}

void LightShafts::_free_buffers() {
	for (int i = 0; i < 2; i++) {
		if (fbos[i] != 0) {
			glDeleteFramebuffers(1, &fbos[i]);
			fbos[i] = 0;
		}
		if (textures[i] != 0) {
			GLES3::Utilities::get_singleton()->texture_free_data(textures[i]);
			textures[i] = 0;
		}
	}
	size = Size2i();
}

void LightShafts::_draw_screen_triangle() {
	glBindVertexArray(screen_triangle_array);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
}

GLuint LightShafts::process(const Parameters &p_params) {
	ERR_FAIL_COND_V(p_params.color == 0 || p_params.depth == 0, 0);

	// A quarter of the width and height, as the RD renderers.
	const Size2i buffer_size = Size2i(MAX(p_params.color_size.x / 4, 1), MAX(p_params.color_size.y / 4, 1));
	if (buffer_size != size) {
		_free_buffers();
		// Half float where the device can render to it (core GLES 3 filters it); otherwise RGB10_A2, holding a quarter
		// of the light so values up to 4 fit, as the frame itself with glow.
		const bool use_float = GLES3::Config::get_singleton()->float_texture_supported;
		encode_scale = use_float ? 1.0f : 0.25f;
		for (int i = 0; i < 2; i++) {
			glGenTextures(1, &textures[i]);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, textures[i]);
			if (use_float) {
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, buffer_size.x, buffer_size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
			} else {
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, buffer_size.x, buffer_size.y, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, nullptr);
			}
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			GLES3::Utilities::get_singleton()->texture_allocated_data(textures[i], buffer_size.x * buffer_size.y * (use_float ? 8 : 4), i == 0 ? "Light shafts mask" : "Light shafts");

			glGenFramebuffers(1, &fbos[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
			GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) {
				WARN_PRINT_ONCE("Could not create the light shafts buffers, status: " + GLES3::TextureStorage::get_singleton()->get_framebuffer_error(status));
				_free_buffers();
				glBindTexture(GL_TEXTURE_2D, 0);
				glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
				return 0;
			}
		}
		size = buffer_size;
	}

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glViewport(0, 0, size.x, size.y);

	const RendererEnvironmentStorage::LightShafts &settings = p_params.settings;
	const float decay = MAX(settings.decay, 0.01f);

	// Mask: the sky above the threshold.
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
		if (!shader.version_bind_shader(shader_version, LightShaftsShaderGLES3::MODE_MASK)) {
			return 0;
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, p_params.color);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, p_params.depth);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		shader.version_set_uniform(LightShaftsShaderGLES3::PIXEL_SIZE, 1.0f / p_params.color_size.x, 1.0f / p_params.color_size.y, shader_version, LightShaftsShaderGLES3::MODE_MASK);
		shader.version_set_uniform(LightShaftsShaderGLES3::LUMINANCE_MULTIPLIER, p_params.luminance_multiplier, shader_version, LightShaftsShaderGLES3::MODE_MASK);
		shader.version_set_uniform(LightShaftsShaderGLES3::THRESHOLD, settings.threshold, shader_version, LightShaftsShaderGLES3::MODE_MASK);
		shader.version_set_uniform(LightShaftsShaderGLES3::ENCODE_SCALE, encode_scale, shader_version, LightShaftsShaderGLES3::MODE_MASK);
		_draw_screen_triangle();

		// The internal color buffer is read nearest by the post pass unless it asks otherwise.
		glActiveTexture(GL_TEXTURE0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// Radial blur toward the sun.
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
		if (!shader.version_bind_shader(shader_version, LightShaftsShaderGLES3::MODE_BLUR)) {
			return 0;
		}
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, textures[0]);
		const float *sun = p_params.sun.position;
		shader.version_set_uniform(LightShaftsShaderGLES3::SUN, sun[0], sun[1], sun[2], sun[3], shader_version, LightShaftsShaderGLES3::MODE_BLUR);
		shader.version_set_uniform(LightShaftsShaderGLES3::DECAY, decay, shader_version, LightShaftsShaderGLES3::MODE_BLUR);
		shader.version_set_uniform(LightShaftsShaderGLES3::MAX_LENGTH, LIGHT_SHAFTS_CUTOFF / decay, shader_version, LightShaftsShaderGLES3::MODE_BLUR);
		shader.version_set_uniform(LightShaftsShaderGLES3::SAMPLE_COUNT, int(MAX(settings.sample_count, 1u)), shader_version, LightShaftsShaderGLES3::MODE_BLUR);
		shader.version_set_uniform(LightShaftsShaderGLES3::ENCODE_SCALE, encode_scale, shader_version, LightShaftsShaderGLES3::MODE_BLUR);
		_draw_screen_triangle();
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, GLES3::TextureStorage::system_fbo);
	return textures[1];
}

#endif // GLES3_ENABLED
