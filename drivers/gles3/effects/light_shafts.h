/**************************************************************************/
/*  light_shafts.h                                                        */
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

#pragma once

#ifdef GLES3_ENABLED

#include "drivers/gles3/shaders/effects/light_shafts.glsl.gen.h"
#include "servers/rendering/storage/environment_storage.h"

namespace GLES3 {

// Screen-space light shafts in the Compatibility renderer (RendererRD::LightShafts is the reference): the mask and the
// radial blur at a quarter of the width and height; post.glsl adds the result to the frame (PostEffects::set_light_shafts).
class LightShafts {
private:
	LightShaftsShaderGLES3 shader;
	RID shader_version;

	GLuint screen_triangle = 0;
	GLuint screen_triangle_array = 0;

	// Mask, then shafts.
	GLuint textures[2] = { 0, 0 };
	GLuint fbos[2] = { 0, 0 };
	Size2i size;
	float encode_scale = 1.0;

	void _free_buffers();
	void _draw_screen_triangle();

public:
	struct Parameters {
		GLuint color = 0; // The internal buffer: sRGB-encoded scene light times the luminance multiplier.
		GLuint depth = 0;
		Size2i color_size;
		float luminance_multiplier = 1.0;
		RendererEnvironmentStorage::LightShafts settings;
		RendererEnvironmentStorage::LightShaftsSun sun;
	};

	// Renders the shafts; returns the texture holding them (light times get_encode_scale()), or 0.
	GLuint process(const Parameters &p_params);
	float get_encode_scale() const { return encode_scale; }

	LightShafts();
	~LightShafts();
};

} // namespace GLES3

#endif // GLES3_ENABLED
