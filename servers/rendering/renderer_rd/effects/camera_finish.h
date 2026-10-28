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

	// 256 bins x (R', G', B', Y') interleaved (data[bin * 4 + channel]), then the four channels' maxima for the drawing.
	static constexpr uint32_t HISTOGRAM_BINS = 256;
	static constexpr uint32_t HISTOGRAM_WORDS = HISTOGRAM_BINS * 4 + 4;

	CameraFinishShaderRD finish_shader;
	RID finish_shader_version;
	PipelineCacheRD finish_pipeline;

	CameraHistogramShaderRD histogram_shader;
	RID histogram_shader_version;
	RID histogram_pipelines[HISTOGRAM_MODE_MAX];
	RID histogram_buffer; // Bound by draws that have no camera histogram of their own; never counted into.

	/* Histogram readback
	 *
	 * Each camera counts into its own buffer, so two cameras rendered in one frame (two SubViewports, or a SubViewport
	 * and the editor's preview) never overwrite each other's counts before they are read. A readback is an
	 * asynchronous buffer_get_data_async(): it is copied to a staging buffer in the frame's command graph and handed
	 * back when RenderingDevice next waits for that frame anyway, so it never stalls; on WebGPU the staging buffer is
	 * mapped with mapAsync right after submit. Results are a frame or more old.
	 */
	struct CameraHistogram {
		RID buffer;
		double last_readback_time = 0.0;
		bool read_back = false; // Whether last_readback_time is set.
	};
	HashMap<RID, CameraHistogram> camera_histograms; // Render thread only.
	static CameraFinish *singleton;

	CameraHistogram *_get_camera_histogram(RID p_camera);
	bool _histogram_readback_due(CameraHistogram &p_histogram, double p_time, float p_interval);
	void _request_histogram_readback(RID p_camera, RID p_buffer, Size2i p_size);
	static void _histogram_readback_done(const PackedByteArray &p_data, RID p_camera, int64_t p_pixel_count);

	/* Clean frame
	 *
	 * Camera3D.get_monitoring_clean_texture(): while a camera asks for it, its image before the overlays (after noise
	 * reduction and sharpening) is copied into a texture of its own, which the server texture the camera holds shows.
	 * One per camera, like the histograms. Nothing is allocated or copied for a camera that does not ask.
	 */
	struct CameraCleanFrame {
		RID texture; // The server texture (Camera3D's), pointed at rd_texture.
		RID rd_texture; // Owned here.
		RID render_target; // The one rd_texture was made for: its formats are the readback's.
		Size2i size;
		RD::DataFormat format = RD::DATA_FORMAT_MAX;
	};
	HashMap<RID, CameraCleanFrame> camera_clean_frames; // Render thread only.

	void _copy_clean_frame(const RendererSceneRender::CameraMonitoring &p_monitoring, RID p_source, RID p_render_target, Size2i p_size, RD::DataFormat p_format);

	void _draw(RID p_source, RID p_dest_framebuffer, RID p_histogram_buffer, const FinishPushConstant &p_push_constant);
	void _compute_histogram(RID p_source, Size2i p_size, bool p_linear_input, RID p_histogram_buffer, bool p_for_drawing);

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
	// Frees a camera's histogram buffer and clean frame (the camera was freed). Its pending readbacks are then discarded.
	void free_camera(RID p_camera);
	// Frees a camera's clean frame (it stopped asking for one).
	void free_camera_clean_frame(RID p_camera);

	CameraFinish();
	~CameraFinish();
};

} // namespace RendererRD
