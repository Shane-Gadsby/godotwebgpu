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

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/light_shafts.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/storage/environment_storage.h"

namespace RendererRD {

// Screen-space light shafts (Environment.light_shafts_*, from the DirectionalLight3D with light_shafts_enabled): raster
// passes so Forward+, Mobile and the web share one path. See light_shafts.glsl for the passes and environment_storage.cpp for
// the model.
class LightShafts {
public:
	struct Parameters {
		RID color; // Added to in place: the frame's color buffer.
		Size2i color_size;
		RID depth;
		bool depth_is_resolved = false; // The MSAA resolve target, an R32F texture, rather than a depth format.
		float luminance_multiplier = 1.0;

		RendererEnvironmentStorage::LightShaftsSun sun;
		float intensity = 1.0;
		float threshold = 1.0;
		float decay = 2.0;
		Color tint = Color(1, 1, 1);
		uint32_t sample_count = 32;
	};

	LightShafts();
	~LightShafts();

	void process(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, const Parameters &p_params);

private:
	enum Mode {
		MODE_MASK,
		MODE_MASK_RESOLVED_DEPTH,
		MODE_BLUR,
		MODE_COMPOSITE,
		MODE_MAX
	};

	struct PushConstant {
		float sun[4];
		float scale[4];
		float threshold;
		float decay;
		float max_length;
		float luminance_multiplier;
		uint32_t sample_count;
		uint32_t pad[3];
	};
	static_assert(sizeof(PushConstant) <= 128, "WebGPU emulates at most 128 bytes of push constants.");

	LightShaftsShaderRD shader;
	RID shader_version;
	PipelineCacheRD pipelines[MODE_MAX];
};

} // namespace RendererRD
