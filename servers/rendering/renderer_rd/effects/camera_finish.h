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

#include "servers/rendering/renderer_rd/pipeline_cache_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/camera_finish.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/camera_histogram.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_scene_render.h"
#include "servers/rendering/storage/camera_attributes_storage.h"

namespace RendererRD {

// The camera finish pass: what a camera does to its image after tonemapping (in-camera noise reduction and
// sharpening, from CameraAttributes) and what its monitor draws over it (Camera3D's monitoring overlays). It runs
// last, on the render target at the output resolution, after the 3D image is complete and before 2D is drawn over
// it, so nothing reads its overlays back. It costs nothing unless something in it is on.
class CameraFinish {
private:
	enum Mode {
		MODE_NOISE_REDUCTION,
		MODE_SHARPEN,
		MODE_OVERLAY,
	};

	enum Flags {
		FLAG_LINEAR_INPUT = 1 << 0,
		FLAG_NR_CHROMA_ONLY = 1 << 1,
		FLAG_ZEBRAS = 1 << 2,
		FLAG_ZEBRAS_SECOND = 1 << 3,
		FLAG_FOCUS_PEAKING = 1 << 4,
		FLAG_FALSE_COLOR = 1 << 5,
		FLAG_HISTOGRAM = 1 << 6,
		FLAG_HISTOGRAM_RGB = 1 << 7,
	};

	// Keep in sync with camera_finish.glsl. 128 bytes: full, the minimum maxPushConstantsSize Vulkan guarantees.
	struct FinishPushConstant {
		float pixel_size[2];
		uint32_t mode;
		uint32_t flags;

		float nr_spatial_sigma;
		float nr_range_sigma;
		float sharpen_amount;
		float sharpen_sigma;

		float sharpen_halo;
		float time;
		float zebras_level;
		float zebras_second_level;

		float stripe_period;
		float peaking_threshold;
		float histogram_opacity;
		float pad;

		float peaking_color[4];
		float histogram_rect[4];

		float zebras_color[4];
		float zebras_second_color[4];
	};
	static_assert(sizeof(FinishPushConstant) == 128);

	struct HistogramPushConstant {
		int32_t size[2];
		uint32_t linear_input;
		uint32_t pad;
	};

	enum HistogramMode {
		HISTOGRAM_COUNT,
		HISTOGRAM_MAX,
		HISTOGRAM_MODE_MAX,
	};

	static constexpr uint32_t HISTOGRAM_WORDS = 256 * 4 + 4;

	CameraFinishShaderRD finish_shader;
	RID finish_shader_version;
	PipelineCacheRD finish_pipeline;

	CameraHistogramShaderRD histogram_shader;
	RID histogram_shader_version;
	RID histogram_pipelines[HISTOGRAM_MODE_MAX];
	RID histogram_buffer;

	void _draw(RID p_source, RID p_dest_framebuffer, const FinishPushConstant &p_push_constant);
	void _compute_histogram(RID p_source, Size2i p_size, bool p_linear_input);

public:
	struct Settings {
		RendererCameraAttributes::CameraFinishSettings camera;
		RendererSceneRender::CameraMonitoring monitoring;
		double time = 0.0; // Seconds, for the zebras' crawl.
	};

	static bool is_active(const Settings &p_settings) {
		return p_settings.camera.noise_reduction || p_settings.camera.sharpening || p_settings.monitoring.is_active();
	}

	// Runs the pass on the render buffers' render target (single view only).
	void process(Ref<RenderSceneBuffersRD> p_render_buffers, const Settings &p_settings);

	CameraFinish();
	~CameraFinish();
};

} // namespace RendererRD
