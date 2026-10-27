/**************************************************************************/
/*  camera_finish.h                                                       */
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

#include "drivers/gles3/shaders/effects/camera_finish.glsl.gen.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering/storage/camera_attributes_storage.h"

namespace GLES3 {

// The camera finish pass of the Compatibility renderer: CameraAttributes' in-camera noise reduction and sharpening
// and Camera3D's zebras, focus peaking and false color, after the 3D image is complete (RendererRD::CameraFinish is
// the reference). There is no histogram here: counting it needs compute or a readback, neither of which this renderer
// has to spare.
class CameraFinish {
private:
	CameraFinishShaderGLES3 shader;
	RID shader_version;

	GLuint screen_triangle = 0;
	GLuint screen_triangle_array = 0;

	// Two scratch copies of the render target, ping-ponged by the passes.
	GLuint scratch_textures[2] = { 0, 0 };
	GLuint scratch_fbos[2] = { 0, 0 };
	Size2i scratch_size;
	GLuint scratch_internal_format = 0;

	void _free_scratch();

public:
	struct Settings {
		RendererCameraAttributes::CameraFinishSettings camera;
		RendererSceneRender::CameraMonitoring monitoring;
		double time = 0.0;
	};

	static bool is_active(const Settings &p_settings) {
		return p_settings.camera.noise_reduction || p_settings.camera.sharpening || p_settings.monitoring.zebras || p_settings.monitoring.zebras_second || p_settings.monitoring.focus_peaking || p_settings.monitoring.false_color;
	}

	void process(RID p_render_target, const Settings &p_settings);

	CameraFinish();
	~CameraFinish();
};

} // namespace GLES3

#endif // GLES3_ENABLED
