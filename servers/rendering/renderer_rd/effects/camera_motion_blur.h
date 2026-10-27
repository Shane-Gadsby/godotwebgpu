/**************************************************************************/
/*  camera_motion_blur.h                                                  */
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

#include "core/math/projection.h"
#include "servers/rendering/renderer_rd/shaders/effects/camera_motion_blur.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

// Camera motion blur and rolling shutter (CameraAttributes.motion_blur_*, rolling_shutter_*), from the velocity
// buffer. Forward+ only: it needs motion vectors and compute. See camera_motion_blur.glsl for the passes and
// camera_attributes_storage.cpp for the model.
class CameraMotionBlur {
public:
	struct Parameters {
		RID color; // Blurred in place; the frame's color buffer at output size.
		Size2i color_size;
		RID depth;
		bool depth_is_resolved = false; // The MSAA resolve target, an R32F texture, rather than a depth format.
		RID velocity;
		Projection reprojection; // Current clip space to the previous frame's, as MotionVectorsStore builds it.
		float z_near = 0.05;
		float z_far = 4000.0;
		bool orthogonal = false;

		float blur_scale = 0.0; // Blur length per unit of a frame's motion.
		float max_length = 0.0; // Longest blur, as a fraction of the frame height.
		float rolling_shutter = 0.0; // Readout time, in frames.
		uint32_t sample_count = 16;
		uint32_t frame = 0;
	};

	CameraMotionBlur();
	~CameraMotionBlur();

	void process(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, const Parameters &p_params);

private:
	enum Mode {
		MODE_PREP,
		MODE_PREP_RESOLVED_DEPTH,
		MODE_TILE_MAX,
		MODE_NEIGHBOR_MAX,
		MODE_GATHER,
		MODE_MAX
	};

	struct PushConstant {
		float reprojection[16];
		float size[2];
		int32_t tile_count[2];
		float blur_scale;
		float max_radius;
		float rolling_shutter;
		float z_near;
		float z_far;
		uint32_t tile_size;
		uint32_t sample_count;
		uint32_t frame;
		uint32_t orthogonal;
		uint32_t pad[3];
	};
	static_assert(sizeof(PushConstant) == 128, "WebGPU emulates at most 128 bytes of push constants.");

	CameraMotionBlurShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
};

} // namespace RendererRD
