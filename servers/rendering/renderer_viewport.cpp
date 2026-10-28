/**************************************************************************/
/*  renderer_viewport.cpp                                                 */
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

#include "renderer_viewport.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/math/transform_interpolator.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/profiling/profiling.h"
#include "servers/display/display_server.h"
#include "servers/rendering/renderer_canvas_cull.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_scene_occlusion_cull.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_method.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/texture_storage.h"

#ifndef XR_DISABLED
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"
#endif // XR_DISABLED

static Transform2D _canvas_get_transform(RendererViewport::Viewport *p_viewport, RendererCanvasCull::Canvas *p_canvas, RendererViewport::Viewport::CanvasData *p_canvas_data, const Vector2 &p_vp_size) {
	Transform2D xf = p_viewport->global_transform;

	Vector2 pixel_snap_offset;
	if (p_viewport->snap_2d_transforms_to_pixel) {
		// We use `floor(p + 0.5)` to snap canvas items, but `ceil(p - 0.5)`
		// to snap viewport transform because the viewport transform is inverse
		// to the camera transform. Also, if the viewport size is not divisible
		// by 2, the center point is offset by 0.5 px and we need to add 0.5
		// before rounding to cancel it out.
		pixel_snap_offset.x = (p_viewport->size.width % 2) ? 0.0 : -0.5;
		pixel_snap_offset.y = (p_viewport->size.height % 2) ? 0.0 : -0.5;
	}

	float scale = 1.0;
	if (p_viewport->canvas_map.has(p_canvas->parent)) {
		Transform2D c_xform = p_viewport->canvas_map[p_canvas->parent].transform;
		if (p_viewport->snap_2d_transforms_to_pixel) {
			c_xform.columns[2] = (c_xform.columns[2] * p_canvas->parent_scale + pixel_snap_offset).ceil() / p_canvas->parent_scale;
		}
		xf = xf * c_xform;
		scale = p_canvas->parent_scale;
	}

	Transform2D c_xform = p_canvas_data->transform;
	if (p_viewport->snap_2d_transforms_to_pixel) {
		c_xform.columns[2] = (c_xform.columns[2] + pixel_snap_offset).ceil();
	}
	xf = xf * c_xform;

	if (scale != 1.0 && !RSG::canvas->disable_scale) {
		Vector2 pivot = p_vp_size * 0.5;
		Transform2D xfpivot;
		xfpivot.set_origin(pivot);
		Transform2D xfscale;
		xfscale.scale(Vector2(scale, scale));

		xf = xfpivot.affine_inverse() * xf;
		xf = xfscale * xf;
		xf = xfpivot * xf;
	}

	return xf;
}

Vector<RendererViewport::Viewport *> RendererViewport::_sort_active_viewports() {
	// We need to sort the viewports in a "topological order", children first and
	// parents last. We also need to keep sibling viewports in the original order
	// from top to bottom.

	Vector<Viewport *> result;
	List<Viewport *> nodes;

	for (int i = active_viewports.size() - 1; i >= 0; --i) {
		Viewport *viewport = active_viewports[i];
		if (viewport->parent.is_valid()) {
			continue;
		}

		nodes.push_back(viewport);
		result.insert(0, viewport);
	}

	while (!nodes.is_empty()) {
		const Viewport *node = nodes.front()->get();
		nodes.pop_front();

		for (int i = active_viewports.size() - 1; i >= 0; --i) {
			Viewport *child = active_viewports[i];
			if (child->parent != node->self) {
				continue;
			}

			if (!nodes.find(child)) {
				nodes.push_back(child);
				result.insert(0, child);
			}
		}
	}

	return result;
}

void RendererViewport::_configure_3d_render_buffers(Viewport *p_viewport) {
	if (p_viewport->render_buffers.is_valid()) {
		if (p_viewport->size.width == 0 || p_viewport->size.height == 0) {
			p_viewport->render_buffers.unref();
		} else {
			const float EPSILON = 0.0001;
			const bool dynamic_scale = _dynamic_scale_active(p_viewport);
			float scaling_3d_scale = dynamic_scale ? p_viewport->dynamic.scale : p_viewport->scaling_3d_scale;
			RSE::ViewportScaling3DMode scaling_3d_mode = p_viewport->scaling_3d_mode;
			bool upscaler_available = p_viewport->fsr_enabled;
			RSE::ViewportScaling3DType scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);

			if ((!upscaler_available || (scaling_type == RSE::VIEWPORT_SCALING_3D_TYPE_SPATIAL)) && scaling_3d_scale >= (1.0 - EPSILON) && scaling_3d_scale <= (1.0 + EPSILON)) {
				// No 3D scaling for spatial modes? Ignore scaling mode, this just introduces overhead.
				// - Mobile can't perform optimal path
				// - FSR does an extra pass (or 2 extra passes if 2D-MSAA is enabled)
				// Scaling = 1.0 on FSR2 and MetalFX temporal has benefits
				scaling_3d_scale = 1.0;
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_OFF;
			}

			if (scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_OFF && scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR && scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_NEAREST && OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
				WARN_PRINT_ONCE("MetalFX and FSR upscaling are not supported in the Compatibility renderer. Falling back to bilinear scaling.");
			}

			if ((scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR || scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR2 || scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) && OS::get_singleton()->get_current_rendering_method() == "mobile") {
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
				WARN_PRINT_ONCE("MetalFX temporal and FSR upscaling are not supported in the Mobile renderer. Falling back to bilinear scaling.");
			}

			if (dynamic_scale && scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR && OS::get_singleton()->get_current_rendering_driver_name() == "webgpu") {
				// FSR 1 renders black on WebGPU; with a scale that moves on its own, use bilinear (user answer Q2).
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
				WARN_PRINT_ONCE("FSR 1.0 3D scaling is not supported with dynamic 3D resolution on WebGPU. Falling back to bilinear scaling.");
			}

			RenderingServerEnums::ViewportMSAA msaa_3d = p_viewport->msaa_3d;

#ifdef RD_ENABLED
			if (scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL && !RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_TEMPORAL)) {
				if (RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_SPATIAL)) {
					// Prefer MetalFX spatial if it is supported, which will be much more efficient than FSR2,
					// as the hardware already will struggle with FSR2.
					scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL;
					WARN_PRINT_ONCE("MetalFX temporal upscaling is not supported by the current renderer or hardware. Falling back to MetalFX Spatial scaling.");
				} else {
					scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_FSR2;
					WARN_PRINT_ONCE("MetalFX upscaling is not supported by the current renderer or hardware. Falling back to FSR 2 scaling.");
				}
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
			}

			if (scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL && !RD::get_singleton()->has_feature(RD::SUPPORTS_METALFX_SPATIAL)) {
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_FSR;
				WARN_PRINT_ONCE("MetalFX spatial upscaling is not supported by the current renderer or hardware. Falling back to FSR scaling.");
			}

			// If MetalFX Temporal upscaling is supported, verify limits.
			if (scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
				double min_scale = (double)RD::get_singleton()->limit_get(RD::LIMIT_METALFX_TEMPORAL_SCALER_MIN_SCALE) / 1000'000.0;
				double max_scale = (double)RD::get_singleton()->limit_get(RD::LIMIT_METALFX_TEMPORAL_SCALER_MAX_SCALE) / 1000'000.0;
				if ((double)scaling_3d_scale < min_scale || (double)scaling_3d_scale > max_scale) {
					scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_FSR2;
					WARN_PRINT_ONCE(vformat("MetalFX temporal upscaling scale is outside limits; scale must be between %f and %f. Falling back to FSR 2 3D resolution scaling.", min_scale, max_scale));
				} else if (msaa_3d != RSE::VIEWPORT_MSAA_DISABLED) {
					WARN_PRINT_ONCE("MetalFX temporal upscaling does not support 3D MSAA. Disabling 3D MSAA internally.");
					msaa_3d = RSE::VIEWPORT_MSAA_DISABLED;
				}
			}
#else
			if (scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_FSR2;
				WARN_PRINT_ONCE("MetalFX upscaling is not supported by the current renderer or hardware. Falling back to FSR 2 scaling.");
			} else if (scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL) {
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_FSR;
				WARN_PRINT_ONCE("MetalFX spatial upscaling is not supported by the current renderer or hardware. Falling back to FSR scaling.");
			}
#endif // RD_ENABLED

			bool scaling_3d_is_not_bilinear = scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_OFF && scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
			bool use_taa = p_viewport->use_taa;

			if (scaling_3d_is_not_bilinear && scaling_3d_scale >= (1.0 + EPSILON)) {
				// FSR, MetalFX, and nearest-neighbor scaling are not designed for downsampling.
				// Fall back to bilinear scaling.
				WARN_PRINT_ONCE("FSR, MetalFX, and nearest-neighbor 3D resolution scaling are not designed for downsampling. Falling back to bilinear 3D resolution scaling.");
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
			}

			if (scaling_3d_is_not_bilinear && scaling_3d_mode != RSE::VIEWPORT_SCALING_3D_MODE_NEAREST && !upscaler_available) {
				// FSR is not actually available.
				// Fall back to bilinear scaling.
				WARN_PRINT_ONCE("FSR 3D resolution scaling is not available. Falling back to bilinear 3D resolution scaling.");
				scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
				scaling_type = RSE::scaling_3d_mode_type(scaling_3d_mode);
			}

			if (use_taa && scaling_type == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
				// Temporal upscalers can't be used with TAA.
				// Turn it off and prefer using the temporal upscaler.
				WARN_PRINT_ONCE("FSR 2 or MetalFX Temporal is not compatible with TAA. Disabling TAA internally.");
				use_taa = false;
			}

			int target_width;
			int target_height;
			int render_width;
			int render_height;

			switch (scaling_3d_mode) {
				case RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR:
				case RSE::VIEWPORT_SCALING_3D_MODE_NEAREST:
					// Clamp 3D rendering resolution to reasonable values supported on most hardware.
					// This prevents freezing the engine or outright crashing on lower-end GPUs.
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = CLAMP(target_width * scaling_3d_scale, 1, 16384);
					render_height = CLAMP(target_height * scaling_3d_scale, 1, 16384);
					break;
				case RSE::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL:
				case RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL:
				case RSE::VIEWPORT_SCALING_3D_MODE_FSR:
				case RSE::VIEWPORT_SCALING_3D_MODE_FSR2:
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = MAX(target_width * scaling_3d_scale, 1.0); // target_width / (target_width * scaling)
					render_height = MAX(target_height * scaling_3d_scale, 1.0);
					break;
				case RSE::VIEWPORT_SCALING_3D_MODE_OFF:
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = target_width;
					render_height = target_height;
					break;
				default:
					// This is an unknown mode.
					WARN_PRINT_ONCE(vformat("Unknown scaling mode: %d. Disabling 3D resolution scaling.", scaling_3d_mode));
					scaling_3d_mode = RSE::VIEWPORT_SCALING_3D_MODE_OFF;
					scaling_3d_scale = 1.0;
					target_width = p_viewport->size.width;
					target_height = p_viewport->size.height;
					render_width = target_width;
					render_height = target_height;
					break;
			}

			uint32_t jitter_phase_count = 0;
			if (scaling_type == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL) {
				// Implementation has been copied from ffxFsr2GetJitterPhaseCount.
				// Also used for MetalFX Temporal scaling.
				jitter_phase_count = uint32_t(8.0f * std::pow(float(target_width) / render_width, 2.0f));
			} else if (use_taa) {
				// Default jitter count for TAA.
				jitter_phase_count = 16;
			}

			p_viewport->internal_size = Size2(render_width, render_height);
			p_viewport->jitter_phase_count = jitter_phase_count;

			// At resolution scales lower than 1.0, use negative texture mipmap bias
			// to compensate for the loss of sharpness.
			const float texture_mipmap_bias = std::log2(MIN(scaling_3d_scale, 1.0)) + p_viewport->texture_mipmap_bias;

			RenderSceneBuffersConfiguration rb_config;
			rb_config.set_render_target(p_viewport->render_target);
			rb_config.set_internal_size(Size2i(render_width, render_height));
			rb_config.set_target_size(Size2(target_width, target_height));
			rb_config.set_view_count(p_viewport->view_count);
			rb_config.set_scaling_3d_mode(scaling_3d_mode);
			rb_config.set_msaa_3d(msaa_3d);
			rb_config.set_screen_space_aa(p_viewport->screen_space_aa);
			rb_config.set_fsr_sharpness(p_viewport->fsr_sharpness);
			rb_config.set_texture_mipmap_bias(texture_mipmap_bias);
			rb_config.set_anisotropic_filtering_level(p_viewport->anisotropic_filtering_level);
			rb_config.set_use_taa(use_taa);
			rb_config.set_use_debanding(p_viewport->use_debanding);
			if (dynamic_scale && scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR2) {
				// FSR 2 accepts any render size up to the one its context was made with: make it at the dynamic maximum
				// once, so no controller change has to grow it (task 74 keeps the context over a reconfigure).
				const float max_scale = MIN(_dynamic_scale_max(p_viewport), 1.0f);
				rb_config.set_max_internal_size(Size2i(MAX(target_width * max_scale, 1.0f), MAX(target_height * max_scale, 1.0f)).max(Size2i(render_width, render_height)));
			}

			p_viewport->render_buffers->configure(&rb_config);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Dynamic 3D resolution (webgpu_notes/work/results/68-dynamic-resolution/DESIGN.md §3). Per viewport, once per drawn
// frame, before the viewport is drawn. Two paths:
// - GPU time (this viewport's vp_begin/vp_end timestamps): lower when the GPU frame is over the budget, straight to
//   the scale the a + b * scale^2 model puts at 85 % of it; raise one step when the model says the next step stays
//   under 80 %. A frame that misses while the GPU has room is CPU-bound: reported, never "fixed" by lowering.
// - Frame time, where there is no GPU time (WebGL, WebGPU without timestamp-query): a step down must show a quarter
//   of the gain the model predicts or it is reverted and reported CPU-bound (with growing back-off); a step up that
//   misses is reverted and that scale is not retried for a while. On WebGPU the submit-to-done latency (queue depth)
//   skips that probe when it is clear either way (user answer Q6).

namespace {
constexpr uint64_t DYNAMIC_STARTUP_USEC = 3000000; // Shader compiles, loading.
constexpr uint32_t DYNAMIC_STARTUP_FRAMES = 60;
constexpr uint64_t DYNAMIC_SETTLE_USEC = 250000;
constexpr uint32_t DYNAMIC_SETTLE_FRAMES = 8;
constexpr uint32_t DYNAMIC_WINDOW = 30; // Samples per decision (median).
constexpr float DYNAMIC_STEP = 0.05;
constexpr float DYNAMIC_HITCH = 4.0; // Frames over 4x the budget (or 4x the recent average) are hitches, not load.
constexpr float DYNAMIC_DOWN_AT = 0.95; // GPU path: lower when over 95 % of the budget...
constexpr float DYNAMIC_DOWN_AIM = 0.85; // ...to the scale the model puts at 85 %.
constexpr float DYNAMIC_DOWN_AIM_FRAME = 0.90; // Frame-time path.
constexpr float DYNAMIC_UP_AT = 0.80; // Raise when the next step stays under 80 %. 80-95 % is the hysteresis band.
constexpr float DYNAMIC_CPU_GPU_ROOM = 0.75; // Misses with the GPU under 75 % of the budget: CPU-bound.
constexpr float DYNAMIC_MISS_FRAME = 1.5; // A frame over 1.5x the budget is a missed frame...
constexpr float DYNAMIC_MISS_MEDIAN = 1.05; // ...and a window whose median is over 1.05x (or a quarter missed) misses.
constexpr float DYNAMIC_VERIFY_GAIN = 0.25; // A frame-time step down must deliver 25 % of its predicted gain.
constexpr float DYNAMIC_VERIFY_STEP = 0.15; // Its size: a smaller step's gain hides under one vsync interval.
constexpr float DYNAMIC_QUEUE_CPU_CLEAR = 2.5; // Frames in flight at or below: CPU-bound, no probe needed.
constexpr float DYNAMIC_QUEUE_GPU_CLEAR = 4.0; // At or above: GPU-bound, no verification needed.
constexpr uint64_t DYNAMIC_GPU_STALE_USEC = 3000000; // GPU path without a fresh result this long: frame time.

float _dynamic_median(LocalVector<float> &p_values) {
	p_values.sort();
	return p_values[p_values.size() / 2];
}

float _dynamic_quantize_down(float p_scale) {
	return Math::floor(p_scale / DYNAMIC_STEP + 0.001f) * DYNAMIC_STEP;
}

int _dynamic_scale_key(float p_scale) {
	return int(Math::round(p_scale / DYNAMIC_STEP));
}
} // namespace

bool RendererViewport::_dynamic_scale_active(const Viewport *p_viewport) const {
	// XR runtimes have their own frame pacing and resolution rules: not in this first version.
	return p_viewport->dynamic.enabled && !p_viewport->use_xr;
}

float RendererViewport::_dynamic_scale_max(const Viewport *p_viewport) const {
	// Only bilinear may go above 1.0 (supersampling); everything else falls back to bilinear there.
	const bool bilinear = p_viewport->scaling_3d_mode == RSE::VIEWPORT_SCALING_3D_MODE_BILINEAR;
	return MAX(p_viewport->dynamic.min_scale, bilinear ? p_viewport->dynamic.max_scale : MIN(p_viewport->dynamic.max_scale, 1.0f));
}

void RendererViewport::_dynamic_scale_publish(Viewport *p_viewport) {
	Viewport::DynamicScale &d = p_viewport->dynamic;
	const bool active = _dynamic_scale_active(p_viewport);
	if (active && d.published_state.load(std::memory_order_relaxed) != d.state && d.state != RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_ADJUSTING && d.state != RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_STARTING) {
		static const char *state_names[RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_MAX] = { "off", "starting", "adjusting", "stable", "CPU-limited", "at minimum scale" };
		print_verbose(vformat("Dynamic 3D resolution: viewport %d %s at scale %.2f (frame %.1f ms, %s %.1f ms, %d changes).", p_viewport->self.get_id(), state_names[d.state], d.scale, d.last_frame_ms,
				d.source == RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_GPU_TIME ? "GPU" : "frame", d.last_measured_ms, d.changes));
	}
	d.published_scale.store(active ? d.scale : p_viewport->scaling_3d_scale, std::memory_order_relaxed);
	d.published_state.store(active ? d.state : RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_OFF, std::memory_order_relaxed);
	d.published_source.store(active ? d.source : RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_NONE, std::memory_order_relaxed);
}

void RendererViewport::_dynamic_scale_restart(Viewport *p_viewport, bool p_startup) {
	Viewport::DynamicScale &d = p_viewport->dynamic;
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (p_startup) {
		d.start_usec = now;
		d.state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_STARTING;
		d.source = RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_NONE;
		d.gpu_samples_at_start = 0;
		d.frames_at_start = 0;
		d.failed_up.clear();
		d.cpu_retry_usec = 0;
		d.cpu_backoff_s = 5.0;
	}
	d.settle_until_usec = now + DYNAMIC_SETTLE_USEC;
	d.settle_frames = DYNAMIC_SETTLE_FRAMES;
	d.frame_window.clear();
	d.gpu_window.clear();
	d.probe_dir = 0;
	// The model is per pixel count: a new size or bounds starts it over.
	d.model_a = -1.0;
	d.point_scale = -1.0;
	_dynamic_scale_publish(p_viewport);
}

void RendererViewport::_dynamic_scale_set(Viewport *p_viewport, float p_scale, const String &p_reason) {
	Viewport::DynamicScale &d = p_viewport->dynamic;
	p_scale = CLAMP(p_scale, d.min_scale, _dynamic_scale_max(p_viewport));
	if (Math::is_equal_approx(p_scale, d.scale)) {
		return;
	}
	print_verbose(vformat("Dynamic 3D resolution: viewport %d scale %.2f -> %.2f (%s).", p_viewport->self.get_id(), d.scale, p_scale, p_reason));
	d.scale = p_scale;
	d.changes++;
	_configure_3d_render_buffers(p_viewport);

	// What was measured so far describes the old scale. The frame time shows a change only once the queued frames
	// drain, and on the web the GPU time comes back a few frames late on top: wait the queue's latency out, twice.
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	uint64_t latency_usec = 0;
#ifdef RD_ENABLED
	if (RD::get_singleton()) {
		latency_usec = RD::get_singleton()->get_submit_to_done_latency_usec();
	}
#endif
	d.settle_until_usec = now + MAX(DYNAMIC_SETTLE_USEC, uint64_t(dynamic_frame_ms * 1000.0f) * DYNAMIC_SETTLE_FRAMES) + 2 * latency_usec;
	d.settle_frames = DYNAMIC_SETTLE_FRAMES;
	d.frame_window.clear();
	d.gpu_window.clear();
	d.state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_ADJUSTING;
	if (d.source == RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_FRAME_TIME) {
		dynamic_frame_time_settle_until_usec = d.settle_until_usec; // Every frame-time viewport sees this change.
	}
	_dynamic_scale_publish(p_viewport);
}

float RendererViewport::_dynamic_scale_update(Viewport *p_viewport, uint64_t p_now, float p_latency_ms) {
	Viewport::DynamicScale &d = p_viewport->dynamic;
	const float budget = 1000.0f / MAX(d.target_fps, 1.0f);
	const float frame_ms = dynamic_frame_ms;

	// This viewport's GPU time, if a new result arrived (on the web the newest result is handed out until a newer one
	// lands, so a repeat is told apart by its begin timestamp).
	float gpu_ms = -1.0;
	if (p_viewport->time_gpu_end > p_viewport->time_gpu_begin && p_viewport->time_gpu_begin != d.last_gpu_begin) {
		d.last_gpu_begin = p_viewport->time_gpu_begin;
		d.last_gpu_sample_usec = p_now;
		gpu_ms = float(double(p_viewport->time_gpu_end - p_viewport->time_gpu_begin) / 1000000.0);
		d.gpu_average_ms = d.gpu_average_ms <= 0.0f ? gpu_ms : d.gpu_average_ms * 0.95f + gpu_ms * 0.05f;
	}

	if (d.state == RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_STARTING) {
		if (gpu_ms > 0.0f) {
			d.gpu_samples_at_start++;
		}
		d.frames_at_start++;
		// Three seconds and enough frames: a scene still loading draws a frame every few seconds, which is too few to
		// tell whether GPU time arrives (camera-sim's unbaked web export loaded for ~50 s).
		if (p_now - d.start_usec < DYNAMIC_STARTUP_USEC || d.frames_at_start < DYNAMIC_STARTUP_FRAMES) {
			return -1.0;
		}
		// Whatever reported GPU time during the start decides the path.
		d.source = d.gpu_samples_at_start >= 10 ? RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_GPU_TIME : RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_FRAME_TIME;
		d.state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_ADJUSTING;
		d.last_gpu_sample_usec = p_now;
		print_verbose(vformat("Dynamic 3D resolution: viewport %d decides from %s.", p_viewport->self.get_id(), d.source == RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_GPU_TIME ? "GPU time" : "frame time"));
		_dynamic_scale_publish(p_viewport);
	}

	const bool gpu_path = d.source == RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_GPU_TIME;
	if (gpu_path && p_now - d.last_gpu_sample_usec > DYNAMIC_GPU_STALE_USEC) {
		// GPU results stopped arriving (a lost feature, a driver that reads 0): fall back for the rest of the run.
		d.source = RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_FRAME_TIME;
		print_verbose(vformat("Dynamic 3D resolution: viewport %d has no GPU time any more, using frame time.", p_viewport->self.get_id()));
		d.frame_window.clear();
		d.gpu_window.clear();
		_dynamic_scale_publish(p_viewport);
		return -1.0;
	}

	// Settle after a change (and, on the frame-time path, after any frame-time viewport's change).
	if (d.settle_frames > 0) {
		d.settle_frames--;
		return -1.0;
	}
	if (p_now < d.settle_until_usec || (!gpu_path && p_now < dynamic_frame_time_settle_until_usec)) {
		return -1.0;
	}

	// A hitch (a compile, a load) is far over both the budget and what the frame usually takes; it says nothing about
	// load. Relative to the average too, or a scene running far below the target would never fill a window.
	if (frame_ms > 0.0f && frame_ms < DYNAMIC_HITCH * MAX(budget, dynamic_frame_average_ms)) {
		d.frame_window.push_back(frame_ms);
	}
	if (gpu_path && gpu_ms > 0.0f && gpu_ms < DYNAMIC_HITCH * MAX(budget, d.gpu_average_ms)) {
		d.gpu_window.push_back(gpu_ms);
	}
	if (d.frame_window.size() < DYNAMIC_WINDOW || (gpu_path && d.gpu_window.size() < DYNAMIC_WINDOW)) {
		return -1.0;
	}

	// A decision: the window is spent whatever happens.
	uint32_t missed = 0;
	float fastest = frame_ms;
	for (float f : d.frame_window) {
		missed += f > DYNAMIC_MISS_FRAME * budget ? 1 : 0;
		fastest = MIN(fastest, f);
	}
	const float frame_med = _dynamic_median(d.frame_window);
	const bool misses = frame_med > DYNAMIC_MISS_MEDIAN * budget || missed >= DYNAMIC_WINDOW / 4;
	const float measured = gpu_path ? _dynamic_median(d.gpu_window) : frame_med;
	d.last_frame_ms = frame_med;
	d.last_measured_ms = measured;
	d.frame_window.clear();
	d.gpu_window.clear();
	// The display's refresh interval, roughly: the fastest frame seen lately (a vsync-bound page cannot be faster).
	dynamic_refresh_ms = dynamic_refresh_ms <= 0.0f ? fastest : MIN(dynamic_refresh_ms * 1.01f, fastest);

	const float s = d.scale;
	const float max_scale = _dynamic_scale_max(p_viewport);
	const bool at_min = s <= d.min_scale + 0.001f;
	const bool at_max = s >= max_scale - 0.001f;

	// The model: measured = a + b * s^2. Refit `a` when the last settled point is at a scale far enough from this one.
	if (d.point_scale > 0.0f && Math::abs(d.point_scale - s) >= 0.1f - 0.001f && p_now - d.point_usec < 20000000) {
		const float b = (measured - d.point_ms) / (s * s - d.point_scale * d.point_scale);
		if (b > 0.0f) {
			d.model_a = CLAMP(measured - b * s * s, 0.0f, 0.6f * measured);
		}
	}
	const float a = d.model_a < 0.0f ? 0.25f * measured : MIN(d.model_a, 0.6f * measured);
	const float b = MAX(measured - a, 0.001f) / MAX(s * s, 0.0001f);
	auto predict = [a, b](float p_scale) { return a + b * p_scale * p_scale; };
	auto scale_for = [a, b](float p_ms) { return Math::sqrt(MAX(p_ms - a, 0.0f) / b); };
	d.point_scale = s;
	d.point_ms = measured;
	d.point_usec = p_now;

	RSE::ViewportScaling3DDynamicState state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_STABLE;
	float new_scale = -1.0;

	if (gpu_path) {
		if (measured > DYNAMIC_DOWN_AT * budget) {
			if (at_min) {
				state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_AT_MIN_SCALE;
			} else {
				new_scale = MAX(MIN(_dynamic_quantize_down(scale_for(DYNAMIC_DOWN_AIM * budget)), s - DYNAMIC_STEP), d.min_scale);
			}
		} else if (!at_max && predict(MIN(s + DYNAMIC_STEP, max_scale)) < DYNAMIC_UP_AT * budget) {
			// Raised even when CPU-bound: resolution is free while the CPU sets the pace.
			new_scale = MIN(_dynamic_quantize_down(s + DYNAMIC_STEP + 0.001f), max_scale);
		} else if (misses && measured < DYNAMIC_CPU_GPU_ROOM * budget) {
			state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_CPU_LIMITED;
		}
		if (new_scale > 0.0f) {
			_dynamic_scale_set(p_viewport, new_scale, vformat("GPU %.1f ms, budget %.1f ms", measured, budget));
			return -1.0; // Applied here: GPU time is per viewport, so viewports do not wait for each other.
		}
		d.state = state;
		_dynamic_scale_publish(p_viewport);
		return -1.0;
	}

	// Frame-time path. A change it proposes is applied by _dynamic_scale_update_all() (one viewport at a time).
	if (d.probe_dir != 0) {
		const int dir = d.probe_dir;
		d.probe_dir = 0;
		if (dir < 0 && d.probe_before_ms - frame_med < DYNAMIC_VERIFY_GAIN * d.probe_predicted_gain) {
			// The step down did not buy its share of the predicted gain: resolution is not what limits the frame.
			// Reverted at once, outside the one-at-a-time rule: it undoes this viewport's own change.
			d.cpu_retry_usec = p_now + uint64_t(d.cpu_backoff_s * 1000000.0f);
			d.cpu_backoff_s = MIN(d.cpu_backoff_s * 2.0f, 60.0f);
			_dynamic_scale_set(p_viewport, d.probe_from, vformat("frame %.1f ms did not improve from %.1f ms: CPU-bound, reverting", frame_med, d.probe_before_ms));
			d.state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_CPU_LIMITED;
			_dynamic_scale_publish(p_viewport);
			return -1.0;
		}
		if (dir < 0) {
			d.cpu_backoff_s = 5.0;
		}
		if (dir > 0 && misses) {
			// The step up missed: back down, and do not try this scale again for a while (longer each time).
			Viewport::DynamicScale::FailedScale &failed = d.failed_up[_dynamic_scale_key(s)];
			failed.until_usec = p_now + uint64_t(failed.backoff_s * 1000000.0f);
			failed.backoff_s = MIN(failed.backoff_s * 2.0f, 120.0f);
			failed.frame_ms = d.probe_before_ms;
			_dynamic_scale_set(p_viewport, d.probe_from, vformat("frame %.1f ms after a step up, reverting", frame_med));
			return -1.0;
		}
	}

	if (misses) {
		if (at_min) {
			state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_AT_MIN_SCALE;
		} else if (p_now < d.cpu_retry_usec) {
			state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_CPU_LIMITED;
		} else {
			// Frames in flight, from the submit-to-done latency (WebGPU only; 0 = unknown).
			const float queue_frames = p_latency_ms > 0.0f ? p_latency_ms / MAX(frame_med, 1.0f) : -1.0f;
			if (queue_frames > 0.0f && queue_frames <= DYNAMIC_QUEUE_CPU_CLEAR) {
				// The GPU keeps up with the CPU: a step down could not help. Say so without probing.
				d.cpu_retry_usec = p_now + uint64_t(d.cpu_backoff_s * 1000000.0f);
				d.cpu_backoff_s = MIN(d.cpu_backoff_s * 2.0f, 60.0f);
				state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_CPU_LIMITED;
				print_verbose(vformat("Dynamic 3D resolution: viewport %d CPU-bound (frame %.1f ms, %.1f frames in flight).", p_viewport->self.get_id(), frame_med, queue_frames));
			} else {
				const bool verify = !(queue_frames >= DYNAMIC_QUEUE_GPU_CLEAR);
				float target = MIN(_dynamic_quantize_down(scale_for(DYNAMIC_DOWN_AIM_FRAME * budget)), s - DYNAMIC_STEP);
				if (verify) {
					// Big enough that its predicted gain shows through vsync quantization and noise. The refresh estimate
					// (the fastest recent frame) is two vsyncs on a GPU-bound page, so it is capped at a quarter frame.
					const float gain_needed = MAX(MIN(dynamic_refresh_ms, 0.25f * frame_med), 0.15f * frame_med);
					target = MIN(target, _dynamic_quantize_down(s - DYNAMIC_VERIFY_STEP + 0.001f));
					while (target - DYNAMIC_STEP >= d.min_scale - 0.001f && frame_med - predict(target) < gain_needed) {
						target -= DYNAMIC_STEP;
					}
				}
				target = MAX(target, d.min_scale);
				d.probe_dir = verify ? -1 : 0;
				d.probe_from = s;
				d.probe_before_ms = frame_med;
				d.probe_predicted_gain = frame_med - predict(target);
				return target;
			}
		}
	} else if (!at_max && frame_med < DYNAMIC_UP_AT * budget && missed == 0) {
		// Probed only when the model also says the step fits: under vsync the frame time is quantized (165 Hz: 12.1
		// or 18.2 ms), and a frame that "fits" one step below often cannot take the next.
		const float target = MIN(_dynamic_quantize_down(s + DYNAMIC_STEP + 0.001f), max_scale);
		const Viewport::DynamicScale::FailedScale *failed = d.failed_up.getptr(_dynamic_scale_key(target));
		// A failure stands until its back-off ends, or until the load got clearly lighter than when it failed.
		const bool blocked = failed != nullptr && p_now < failed->until_usec && frame_med > 0.75f * failed->frame_ms;
		if (!blocked && predict(target) < DYNAMIC_UP_AT * budget) {
			d.probe_dir = 1;
			d.probe_from = s;
			d.probe_before_ms = frame_med;
			return target;
		}
	}
	d.state = state;
	_dynamic_scale_publish(p_viewport);
	return -1.0;
}

void RendererViewport::_dynamic_scale_update_all() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	dynamic_frame_ms = dynamic_last_frame_usec > 0 ? float(now - dynamic_last_frame_usec) / 1000.0f : 0.0f;
	dynamic_last_frame_usec = now;
	if (dynamic_frame_ms > 0.0f && dynamic_frame_ms < 1000.0f) {
		// About 20 frames: follows sustained load, barely moves for one hitch.
		dynamic_frame_average_ms = dynamic_frame_average_ms <= 0.0f ? dynamic_frame_ms : dynamic_frame_average_ms * 0.95f + dynamic_frame_ms * 0.05f;
	}

	float latency_ms = 0.0;
#ifdef RD_ENABLED
	if (RD::get_singleton()) {
		latency_ms = float(RD::get_singleton()->get_submit_to_done_latency_usec()) / 1000.0f;
	}
#endif

	// Frame time is one number for every viewport: only one frame-time viewport changes per settle, the one with
	// the most pixels first (user answer Q5). GPU-time viewports measure themselves and change on their own.
	Viewport *frame_time_pick = nullptr;
	float frame_time_scale = -1.0;
	for (Viewport *vp : sorted_active_viewports) {
		if (!vp->dynamic.enabled || vp->last_pass != draw_viewports_pass) {
			continue;
		}
		if (vp->use_xr) {
			WARN_PRINT_ONCE("Dynamic 3D resolution is not supported on XR viewports; it is disabled there.");
			continue;
		}
		if (!vp->render_buffers.is_valid()) {
			continue; // No 3D drawn.
		}
		const float proposed = _dynamic_scale_update(vp, now, latency_ms);
		if (proposed <= 0.0f) {
			continue;
		}
		if (frame_time_pick == nullptr || vp->size.width * vp->size.height > frame_time_pick->size.width * frame_time_pick->size.height) {
			if (frame_time_pick != nullptr) {
				frame_time_pick->dynamic.probe_dir = 0; // Its turn is not now; it decides again after the settle.
			}
			frame_time_pick = vp;
			frame_time_scale = proposed;
		} else {
			vp->dynamic.probe_dir = 0;
		}
	}
	if (frame_time_pick != nullptr) {
		const Viewport::DynamicScale &d = frame_time_pick->dynamic;
		_dynamic_scale_set(frame_time_pick, frame_time_scale, vformat("frame %.1f ms, budget %.1f ms%s", d.probe_before_ms, 1000.0f / MAX(d.target_fps, 1.0f), d.probe_dir < 0 ? ", verifying" : (d.probe_dir > 0 ? ", trying a step up" : "")));
	}
}

void RendererViewport::_draw_3d(Viewport *p_viewport) {
#ifndef _3D_DISABLED
	RENDER_TIMESTAMP("> Render 3D Scene");

	if (p_viewport->use_occlusion_culling) {
		if (p_viewport->occlusion_buffer_dirty) {
			float aspect = p_viewport->size.aspect();
			int max_size = occlusion_rays_per_thread * WorkerThreadPool::get_singleton()->get_thread_count();

			int viewport_size = p_viewport->size.width * p_viewport->size.height;
			max_size = CLAMP(max_size, viewport_size / (32 * 32), viewport_size / (2 * 2)); // At least one depth pixel for every 16x16 region. At most one depth pixel for every 2x2 region.

			float height = Math::sqrt(max_size / aspect);
			Size2i new_size = Size2i(height * aspect, height);
			RendererSceneOcclusionCull::get_singleton()->buffer_set_size(p_viewport->self, new_size);
			p_viewport->occlusion_buffer_dirty = false;
		}
	}

	float screen_mesh_lod_threshold = p_viewport->mesh_lod_threshold / float(p_viewport->size.width);
	RSG::scene->render_camera(p_viewport->render_buffers, p_viewport->camera, p_viewport->scenario, p_viewport->self, p_viewport->internal_size, p_viewport->jitter_phase_count, screen_mesh_lod_threshold, p_viewport->shadow_atlas, p_viewport->window_output_max_value, &p_viewport->render_info);

	RENDER_TIMESTAMP("< Render 3D Scene");
#endif // _3D_DISABLED
}

void RendererViewport::_draw_viewport(Viewport *p_viewport) {
	// The dynamic 3D resolution controller reads this viewport's GPU time from the same timestamps.
	const bool measure_render_time = p_viewport->measure_render_time || _dynamic_scale_active(p_viewport);
	if (measure_render_time) {
		String rt_id = "vp_begin_" + itos(p_viewport->self.get_id());
		RSG::utilities->capture_timestamp(rt_id);
		timestamp_vp_map[rt_id] = p_viewport->self;
	}

	if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
		// This is currently needed for GLES to keep the current window being rendered to up to date
		DisplayServer::get_singleton()->gl_window_make_current(p_viewport->viewport_to_screen);
	}

	/* Camera should always be BEFORE any other 3D */

	bool can_draw_2d = !p_viewport->disable_2d && p_viewport->view_count == 1; // Stereo rendering does not support 2D, no depth data
	bool scenario_draw_canvas_bg = false; //draw canvas, or some layer of it, as BG for 3D instead of in front
	int scenario_canvas_max_layer = 0;
	bool force_clear_render_target = false;

	for (int i = 0; i < RSE::VIEWPORT_RENDER_INFO_TYPE_MAX; i++) {
		for (int j = 0; j < RSE::VIEWPORT_RENDER_INFO_MAX; j++) {
			p_viewport->render_info.info[i][j] = 0;
		}
	}

	if (RSG::scene->is_scenario(p_viewport->scenario)) {
		RID environment = RSG::scene->scenario_get_environment(p_viewport->scenario);
		if (RSG::scene->is_environment(environment)) {
			if (can_draw_2d && !viewport_is_environment_disabled(p_viewport)) {
				scenario_draw_canvas_bg = RSG::scene->environment_get_background(environment) == RSE::ENV_BG_CANVAS;
				scenario_canvas_max_layer = RSG::scene->environment_get_canvas_max_layer(environment);
			} else if (RSG::scene->environment_get_background(environment) == RSE::ENV_BG_CANVAS) {
				// The scene renderer will still copy over the last frame, so we need to clear the render target.
				force_clear_render_target = true;
			}
		}

		p_viewport->window_output_max_value = 1.0;
#ifdef RD_ENABLED
		DisplayServerEnums::WindowID parent_window = _get_containing_window(p_viewport);
		if (RD::get_singleton() && parent_window != DisplayServerEnums::INVALID_WINDOW_ID) {
			RenderingContextDriver *context_driver = RD::get_singleton()->get_context_driver();
			if (context_driver->window_get_hdr_output_enabled(parent_window)) {
				p_viewport->window_output_max_value = context_driver->window_get_output_max_linear_value(parent_window);
			}
		}
#endif
	}

	bool can_draw_3d = RSG::scene->is_camera(p_viewport->camera) && !p_viewport->disable_3d;

	if ((scenario_draw_canvas_bg || can_draw_3d) && !p_viewport->render_buffers.is_valid()) {
		//wants to draw 3D but there is no render buffer, create
		p_viewport->render_buffers = RSG::scene->render_buffers_create();

		_configure_3d_render_buffers(p_viewport);
	}

	Color bgcolor = p_viewport->transparent_bg ? Color(0, 0, 0, 0) : RSG::texture_storage->get_default_clear_color();

	if (p_viewport->clear_mode != RSE::VIEWPORT_CLEAR_NEVER) {
		RSG::texture_storage->render_target_request_clear(p_viewport->render_target, bgcolor);
		if (p_viewport->clear_mode == RSE::VIEWPORT_CLEAR_ONLY_NEXT_FRAME) {
			p_viewport->clear_mode = RSE::VIEWPORT_CLEAR_NEVER;
		}
	}

	if (!scenario_draw_canvas_bg && can_draw_3d) {
		if (force_clear_render_target) {
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
		}
		_draw_3d(p_viewport);
	}

	if (can_draw_2d) {
		RBMap<Viewport::CanvasKey, Viewport::CanvasData *> canvas_map;

		Rect2 clip_rect(0, 0, p_viewport->size.x, p_viewport->size.y);
		RendererCanvasRender::Light *lights = nullptr;
		RendererCanvasRender::Light *lights_with_shadow = nullptr;

		RendererCanvasRender::Light *directional_lights = nullptr;
		RendererCanvasRender::Light *directional_lights_with_shadow = nullptr;

		if (p_viewport->sdf_active) {
			// Process SDF.

			Rect2 sdf_rect = RSG::texture_storage->render_target_get_sdf_rect(p_viewport->render_target);

			RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

			// Make list of occluders.
			for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
				RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
				Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

				for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
					if (!F->enabled) {
						continue;
					}

					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
						F->xform_cache = xf * F->xform_curr;
					} else {
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
						F->xform_cache = xf * F->xform_cache;
					}

					if (sdf_rect.intersects_transformed(F->xform_cache, F->aabb_cache)) {
						F->next = occluders;
						occluders = F;
					}
				}
			}

			RSG::canvas_render->render_sdf(p_viewport->render_target, occluders);
			RSG::texture_storage->render_target_mark_sdf_enabled(p_viewport->render_target, true);

			p_viewport->sdf_active = false; // If used, gets set active again.
		} else {
			RSG::texture_storage->render_target_mark_sdf_enabled(p_viewport->render_target, false);
		}

		Rect2 shadow_rect;

		int shadow_count = 0;
		int directional_light_count = 0;

		RENDER_TIMESTAMP("Cull 2D Lights");
		for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
			RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);

			Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

			// Find lights in canvas.

			for (RendererCanvasRender::Light *F : canvas->lights) {
				RendererCanvasRender::Light *cl = F;
				if (cl->enabled && cl->texture.is_valid()) {
					//not super efficient..
					Size2 tsize = RSG::texture_storage->texture_size_with_proxy(cl->texture);
					tsize *= cl->scale;

					Vector2 offset = tsize / 2.0;
					Rect2 local_rect = Rect2(-offset + cl->texture_offset, tsize);

					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !cl->interpolated) {
						cl->xform_cache = xf * cl->xform_curr;
					} else {
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(cl->xform_prev, cl->xform_curr, cl->xform_cache, f);
						cl->xform_cache = xf * cl->xform_cache;
					}

					cl->rect_cache = cl->xform_cache.xform(local_rect);

					if (clip_rect.intersects(cl->rect_cache)) {
						cl->filter_next_ptr = lights;
						lights = cl;
						Transform2D scale;
						scale.scale(local_rect.size);
						scale.columns[2] = local_rect.position;
						cl->light_shader_xform = cl->xform_cache * scale;
						if (cl->use_shadow) {
							cl->shadows_next_ptr = lights_with_shadow;
							if (lights_with_shadow == nullptr) {
								shadow_rect = cl->rect_cache;
							} else {
								shadow_rect = shadow_rect.merge(cl->rect_cache);
							}
							lights_with_shadow = cl;
							cl->radius_cache = local_rect.size.length();
						}
					}
				}
			}

			for (RendererCanvasRender::Light *F : canvas->directional_lights) {
				RendererCanvasRender::Light *cl = F;
				if (cl->enabled) {
					cl->filter_next_ptr = directional_lights;
					directional_lights = cl;
					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !cl->interpolated) {
						cl->xform_cache = xf * cl->xform_curr;
					} else {
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(cl->xform_prev, cl->xform_curr, cl->xform_cache, f);
						cl->xform_cache = xf * cl->xform_cache;
					}
					cl->xform_cache.columns[2] = Vector2(); //translation is pointless
					if (cl->use_shadow) {
						cl->shadows_next_ptr = directional_lights_with_shadow;
						directional_lights_with_shadow = cl;
					}

					directional_light_count++;

					if (directional_light_count == RSE::MAX_2D_DIRECTIONAL_LIGHTS) {
						break;
					}
				}
			}

			canvas_map[Viewport::CanvasKey(E.key, E.value.layer, E.value.sublayer)] = &E.value;
		}

		if (lights_with_shadow) {
			//update shadows if any

			RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

			RENDER_TIMESTAMP("> Render PointLight2D Shadows");
			RENDER_TIMESTAMP("Cull LightOccluder2Ds");

			//make list of occluders
			for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
				RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
				Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

				for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
					if (!F->enabled) {
						continue;
					}
					if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
						F->xform_cache = xf * F->xform_curr;
					} else {
						real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
						TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
						F->xform_cache = xf * F->xform_cache;
					}
					if (shadow_rect.intersects_transformed(F->xform_cache, F->aabb_cache)) {
						F->next = occluders;
						occluders = F;
					}
				}
			}
			//update the light shadowmaps with them

			RendererCanvasRender::Light *light = lights_with_shadow;
			while (light) {
				RENDER_TIMESTAMP("Render PointLight2D Shadow");

				RSG::canvas_render->light_update_shadow(light->light_internal, shadow_count++, light->xform_cache.affine_inverse(), light->item_shadow_mask, light->radius_cache / 1000.0, light->radius_cache * 1.1, occluders, light->rect_cache);
				light = light->shadows_next_ptr;
			}

			RENDER_TIMESTAMP("< Render PointLight2D Shadows");
		}

		if (directional_lights_with_shadow) {
			//update shadows if any
			RendererCanvasRender::Light *light = directional_lights_with_shadow;
			while (light) {
				Vector2 light_dir = -light->xform_cache.columns[1].normalized(); // Y is light direction
				float cull_distance = light->directional_distance;

				Vector2 light_dir_sign;
				light_dir_sign.x = (Math::abs(light_dir.x) < CMP_EPSILON) ? 0.0 : ((light_dir.x > 0.0) ? 1.0 : -1.0);
				light_dir_sign.y = (Math::abs(light_dir.y) < CMP_EPSILON) ? 0.0 : ((light_dir.y > 0.0) ? 1.0 : -1.0);

				Vector2 points[6];
				int point_count = 0;

				for (int j = 0; j < 4; j++) {
					static const Vector2 signs[4] = { Vector2(1, 1), Vector2(1, 0), Vector2(0, 0), Vector2(0, 1) };
					Vector2 sign_cmp = signs[j] * 2.0 - Vector2(1.0, 1.0);
					Vector2 point = clip_rect.position + clip_rect.size * signs[j];

					if (sign_cmp == light_dir_sign) {
						//both point in same direction, plot offsetted
						points[point_count++] = point + light_dir * cull_distance;
					} else if (sign_cmp.x == light_dir_sign.x || sign_cmp.y == light_dir_sign.y) {
						int next_j = (j + 1) % 4;
						Vector2 next_sign_cmp = signs[next_j] * 2.0 - Vector2(1.0, 1.0);

						//one point in the same direction, plot segment

						if (next_sign_cmp.x == light_dir_sign.x || next_sign_cmp.y == light_dir_sign.y) {
							if (light_dir_sign.x != 0.0 || light_dir_sign.y != 0.0) {
								points[point_count++] = point;
							}
							points[point_count++] = point + light_dir * cull_distance;
						} else {
							points[point_count++] = point + light_dir * cull_distance;
							if (light_dir_sign.x != 0.0 || light_dir_sign.y != 0.0) {
								points[point_count++] = point;
							}
						}
					} else {
						//plot normally
						points[point_count++] = point;
					}
				}

				Vector2 xf_points[6];

				RendererCanvasRender::LightOccluderInstance *occluders = nullptr;

				RENDER_TIMESTAMP("> Render DirectionalLight2D Shadows");

				// Make list of occluders.
				for (KeyValue<RID, Viewport::CanvasData> &E : p_viewport->canvas_map) {
					RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value.canvas);
					Transform2D xf = _canvas_get_transform(p_viewport, canvas, &E.value, clip_rect.size);

					for (RendererCanvasRender::LightOccluderInstance *F : canvas->occluders) {
						if (!F->enabled) {
							continue;
						}
						if (!RSG::canvas->_interpolation_data.interpolation_enabled || !F->interpolated) {
							F->xform_cache = xf * F->xform_curr;
						} else {
							real_t f = Engine::get_singleton()->get_physics_interpolation_fraction();
							TransformInterpolator::interpolate_transform_2d(F->xform_prev, F->xform_curr, F->xform_cache, f);
							F->xform_cache = xf * F->xform_cache;
						}
						Transform2D localizer = F->xform_cache.affine_inverse();

						for (int j = 0; j < point_count; j++) {
							xf_points[j] = localizer.xform(points[j]);
						}
						if (F->aabb_cache.intersects_filled_polygon(xf_points, point_count)) {
							F->next = occluders;
							occluders = F;
						}
					}
				}

				RSG::canvas_render->light_update_directional_shadow(light->light_internal, shadow_count++, light->xform_cache, light->item_shadow_mask, cull_distance, clip_rect, occluders);

				light = light->shadows_next_ptr;
			}

			RENDER_TIMESTAMP("< Render DirectionalLight2D Shadows");
		}

		if (scenario_draw_canvas_bg && canvas_map.begin() && canvas_map.begin()->key.get_layer() > scenario_canvas_max_layer) {
			// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
			// Clear now otherwise we copy over garbage from the render target.
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
			if (!can_draw_3d) {
				RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas, p_viewport->window_output_max_value);
			} else {
				_draw_3d(p_viewport);
			}
			scenario_draw_canvas_bg = false;
		}

		int canvas_idx = 0;
		for (const KeyValue<Viewport::CanvasKey, Viewport::CanvasData *> &E : canvas_map) {
			RendererCanvasCull::Canvas *canvas = static_cast<RendererCanvasCull::Canvas *>(E.value->canvas);

			Transform2D xform = _canvas_get_transform(p_viewport, canvas, E.value, clip_rect.size);

			RendererCanvasRender::Light *canvas_lights = nullptr;
			RendererCanvasRender::Light *canvas_directional_lights = nullptr;

			RendererCanvasRender::Light *ptr = lights;
			while (ptr) {
				if (E.value->layer >= ptr->layer_min && E.value->layer <= ptr->layer_max) {
					ptr->next_ptr = canvas_lights;
					canvas_lights = ptr;
				}
				ptr = ptr->filter_next_ptr;
			}

			ptr = directional_lights;
			while (ptr) {
				if (E.value->layer >= ptr->layer_min && E.value->layer <= ptr->layer_max) {
					ptr->next_ptr = canvas_directional_lights;
					canvas_directional_lights = ptr;
				}
				ptr = ptr->filter_next_ptr;
			}

			RENDER_TIMESTAMP("> Render Canvas " + itos(canvas_idx));

			RSG::canvas->render_canvas(p_viewport->render_target, canvas, xform, canvas_lights, canvas_directional_lights, clip_rect, p_viewport->texture_filter, p_viewport->texture_repeat, p_viewport->snap_2d_transforms_to_pixel, p_viewport->snap_2d_vertices_to_pixel, p_viewport->canvas_cull_mask, &p_viewport->render_info);

			RENDER_TIMESTAMP("< Render Canvas " + itos(canvas_idx));

			if (RSG::canvas->was_sdf_used()) {
				p_viewport->sdf_active = true;
			}

			if (scenario_draw_canvas_bg && E.key.get_layer() >= scenario_canvas_max_layer) {
				// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
				// Clear now otherwise we copy over garbage from the render target.
				RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
				if (!can_draw_3d) {
					RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas, p_viewport->window_output_max_value);
				} else {
					_draw_3d(p_viewport);
				}

				scenario_draw_canvas_bg = false;
			}

			canvas_idx++;
		}

		if (scenario_draw_canvas_bg) {
			// There may be an outstanding clear request if a clear was requested, but no 2D elements were drawn.
			// Clear now otherwise we copy over garbage from the render target.
			RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
			if (!can_draw_3d) {
				RSG::scene->render_empty_scene(p_viewport->render_buffers, p_viewport->scenario, p_viewport->shadow_atlas, p_viewport->window_output_max_value);
			} else {
				_draw_3d(p_viewport);
			}
		}
	}

	if (RSG::texture_storage->render_target_is_clear_requested(p_viewport->render_target)) {
		//was never cleared in the end, force clear it
		RSG::texture_storage->render_target_do_clear_request(p_viewport->render_target);
	}

	if (RSG::texture_storage->render_target_get_msaa_needs_resolve(p_viewport->render_target)) {
		WARN_PRINT_ONCE("2D MSAA is enabled while there is no 2D content. Disable 2D MSAA for better performance.");
		RSG::texture_storage->render_target_do_msaa_resolve(p_viewport->render_target);
	}

	if (measure_render_time) {
		String rt_id = "vp_end_" + itos(p_viewport->self.get_id());
		RSG::utilities->capture_timestamp(rt_id);
		timestamp_vp_map[rt_id] = p_viewport->self;
	}
}

DisplayServerEnums::WindowID RendererViewport::_get_containing_window(Viewport *p_viewport) {
	if (p_viewport->viewport_to_screen != DisplayServerEnums::INVALID_WINDOW_ID) {
		return p_viewport->viewport_to_screen;
	}

	if (p_viewport->parent.is_valid()) {
		Viewport *parent = viewport_owner.get_or_null(p_viewport->parent);
		if (parent) {
			return _get_containing_window(parent);
		}
	}

	return DisplayServerEnums::INVALID_WINDOW_ID;
}

void RendererViewport::draw_viewports(bool p_swap_buffers) {
	GodotProfileZoneGroupedFirst(_profile_zone, "prepare viewports");
	timestamp_vp_map.clear();

#ifndef XR_DISABLED
	// get our xr interface in case we need it
	Ref<XRInterface> xr_interface;
	XRServer *xr_server = XRServer::get_singleton();
	if (xr_server != nullptr) {
		// retrieve the interface responsible for rendering
		xr_interface = xr_server->get_primary_interface();
	}
#endif // XR_DISABLED

	if (Engine::get_singleton()->is_editor_hint()) {
		RSG::texture_storage->set_default_clear_color(GLOBAL_GET_CACHED(Color, "rendering/environment/defaults/default_clear_color"));
	}

	if (sorted_active_viewports_dirty) {
		GodotProfileZoneGrouped(_profile_zone, "_sort_active_viewports");
		sorted_active_viewports = _sort_active_viewports();
		sorted_active_viewports_dirty = false;
	}

	HashMap<DisplayServerEnums::WindowID, Vector<RenderingServerTypes::BlitToScreen>> blit_to_screen_list;
	//draw viewports
	RENDER_TIMESTAMP("> Render Viewports");

	GodotProfileZoneGrouped(_profile_zone, "render viewports");

	//determine what is visible
	draw_viewports_pass++;

	for (int i = sorted_active_viewports.size() - 1; i >= 0; i--) { //to compute parent dependency, must go in reverse draw order
		Viewport *vp = sorted_active_viewports[i];

		if (vp->update_mode == RSE::VIEWPORT_UPDATE_DISABLED) {
			continue;
		}

		if (!vp->render_target.is_valid()) {
			continue;
		}
		//ERR_CONTINUE(!vp->render_target.is_valid());

		bool visible = vp->viewport_to_screen_rect != Rect2();

#ifndef XR_DISABLED
		if (vp->use_xr) {
			visible = xr_interface.is_valid();
		} else
#endif // XR_DISABLED
		{
			if (vp->update_mode == RSE::VIEWPORT_UPDATE_ALWAYS || vp->update_mode == RSE::VIEWPORT_UPDATE_ONCE) {
				visible = true;
			}

			if (vp->update_mode == RSE::VIEWPORT_UPDATE_WHEN_VISIBLE && RSG::texture_storage->render_target_was_used(vp->render_target)) {
				visible = true;
			}

			if (vp->update_mode == RSE::VIEWPORT_UPDATE_WHEN_PARENT_VISIBLE) {
				Viewport *parent = viewport_owner.get_or_null(vp->parent);
				if (parent && parent->last_pass == draw_viewports_pass) {
					visible = true;
				}
			}
		}

		visible = visible && vp->size.x > 1 && vp->size.y > 1 && vp->view_count > 0;

		// Diagnostic: log each viewport's visibility on first 3 frames.
		{
		}

		if (visible) {
			vp->last_pass = draw_viewports_pass;
		}
	}

	if (dynamic_viewport_count > 0) {
		GodotProfileZoneGrouped(_profile_zone, "dynamic 3D resolution");
		_dynamic_scale_update_all();
	}

	int vertices_drawn = 0;
	int objects_drawn = 0;
	int draw_calls_used = 0;

	for (int i = 0; i < sorted_active_viewports.size(); i++) {
		// TODO Somehow print the index
		GodotProfileZone("render viewport");

		Viewport *vp = sorted_active_viewports[i];

		if (vp->last_pass != draw_viewports_pass) {
			continue; //should not draw
		}

		RENDER_TIMESTAMP("> Render Viewport " + itos(i));

		RSG::texture_storage->render_target_set_as_unused(vp->render_target);
#ifndef XR_DISABLED
		if (vp->use_xr && xr_interface.is_valid()) {
			// Inform XR interface we're about to render its viewport,
			// if this returns false we don't render.
			// This usually is a result of the player taking off their headset and OpenXR telling us to skip
			// rendering frames.
			if (xr_interface->pre_draw_viewport(vp->render_target)) {
				RSG::texture_storage->render_target_set_override(vp->render_target,
						xr_interface->get_color_texture(),
						xr_interface->get_depth_texture(),
						xr_interface->get_velocity_texture(),
						xr_interface->get_velocity_depth_texture());

				RSG::texture_storage->render_target_set_velocity_target_size(vp->render_target, xr_interface->get_velocity_target_size());

				if (xr_interface->get_velocity_texture().is_valid()) {
					_viewport_set_force_motion_vectors(vp, true);
				} else {
					_viewport_set_force_motion_vectors(vp, false);
				}

				RSG::texture_storage->render_target_set_render_region(vp->render_target, xr_interface->get_render_region());

				// render...
				RSG::scene->set_debug_draw_mode(vp->debug_draw);

				// and draw viewport
				_draw_viewport(vp);

				// commit our eyes
				Vector<RenderingServerTypes::BlitToScreen> blits = xr_interface->post_draw_viewport(vp->render_target, vp->viewport_to_screen_rect);
				if (vp->viewport_to_screen != DisplayServerEnums::INVALID_WINDOW_ID) {
					if (RSG::rasterizer->is_opengl()) {
						if (blits.size() > 0) {
							RSG::rasterizer->blit_render_targets_to_screen(vp->viewport_to_screen, blits.ptr(), blits.size());
							RSG::rasterizer->gl_end_frame(p_swap_buffers);
						}
					} else if (blits.size() > 0) {
						if (!blit_to_screen_list.has(vp->viewport_to_screen)) {
							blit_to_screen_list[vp->viewport_to_screen] = Vector<RenderingServerTypes::BlitToScreen>();
						}

						for (int b = 0; b < blits.size(); b++) {
							blit_to_screen_list[vp->viewport_to_screen].push_back(blits[b]);
						}
					}
				}
			}
		} else
#endif // XR_DISABLED
		{
			RSG::scene->set_debug_draw_mode(vp->debug_draw);

			// render standard mono camera
			_draw_viewport(vp);

			if (vp->viewport_to_screen != DisplayServerEnums::INVALID_WINDOW_ID && (!vp->viewport_render_direct_to_screen || !RSG::rasterizer->is_low_end())) {
				//copy to screen if set as such
				RenderingServerTypes::BlitToScreen blit;
				blit.render_target = vp->render_target;
				if (vp->viewport_to_screen_rect != Rect2()) {
					blit.dst_rect = vp->viewport_to_screen_rect;
				} else {
					blit.dst_rect.position = Vector2();
					blit.dst_rect.size = vp->size;
				}

				if (RSG::rasterizer->is_opengl()) {
					RSG::rasterizer->blit_render_targets_to_screen(vp->viewport_to_screen, &blit, 1);
					RSG::rasterizer->gl_end_frame(p_swap_buffers);
				} else {
					Vector<RenderingServerTypes::BlitToScreen> *blits = blit_to_screen_list.getptr(vp->viewport_to_screen);
					if (blits == nullptr) {
						blits = &blit_to_screen_list.insert(vp->viewport_to_screen, Vector<RenderingServerTypes::BlitToScreen>())->value;
					}
					blits->push_back(blit);
				}
			}
		}

		if (vp->update_mode == RSE::VIEWPORT_UPDATE_ONCE) {
			vp->update_mode = RSE::VIEWPORT_UPDATE_DISABLED;
		}

		RENDER_TIMESTAMP("< Render Viewport " + itos(i));

		// 3D render info.
		objects_drawn += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME] + vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME];
		vertices_drawn += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME] + vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME];
		draw_calls_used += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_VISIBLE][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME] + vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_SHADOW][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME];
		// 2D render info.
		objects_drawn += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME];
		vertices_drawn += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_PRIMITIVES_IN_FRAME];
		draw_calls_used += vp->render_info.info[RSE::VIEWPORT_RENDER_INFO_TYPE_CANVAS][RSE::VIEWPORT_RENDER_INFO_DRAW_CALLS_IN_FRAME];
	}

	RSG::scene->set_debug_draw_mode(RSE::VIEWPORT_DEBUG_DRAW_DISABLED);

	total_objects_drawn = objects_drawn;
	total_vertices_drawn = vertices_drawn;
	total_draw_calls_used = draw_calls_used;

	RENDER_TIMESTAMP("< Render Viewports");

	GodotProfileZoneGrouped(_profile_zone, "rasterizer->blit_render_targets_to_screen");
	if (p_swap_buffers && !blit_to_screen_list.is_empty()) {
		for (const KeyValue<int, Vector<RenderingServerTypes::BlitToScreen>> &E : blit_to_screen_list) {
			RSG::rasterizer->blit_render_targets_to_screen(E.key, E.value.ptr(), E.value.size());
		}
	}
}

RID RendererViewport::viewport_allocate() {
	return viewport_owner.allocate_rid();
}

void RendererViewport::viewport_initialize(RID p_rid) {
	viewport_owner.initialize_rid(p_rid);
	Viewport *viewport = viewport_owner.get_or_null(p_rid);
	viewport->self = p_rid;
	viewport->render_target = RSG::texture_storage->render_target_create();
	viewport->shadow_atlas = RSG::light_storage->shadow_atlas_create();
	viewport->viewport_render_direct_to_screen = false;

	viewport->fsr_enabled = !RSG::rasterizer->is_low_end() && !viewport->disable_3d;
}

#ifndef XR_DISABLED
void RendererViewport::viewport_set_use_xr(RID p_viewport, bool p_use_xr) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_xr == p_use_xr) {
		return;
	}

	viewport->use_xr = p_use_xr;

	// Re-configure the 3D render buffers when disabling XR. They'll get
	// re-configured when enabling XR in draw_viewports().
	if (!p_use_xr) {
		viewport->view_count = 1;
		_configure_3d_render_buffers(viewport);
	}
}
#endif // !XR_DISABLED

void RendererViewport::viewport_set_scaling_3d_mode(RID p_viewport, RSE::ViewportScaling3DMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	const String rendering_method = OS::get_singleton()->get_current_rendering_method();
	if (rendering_method != "forward_plus") {
		if (p_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR) {
			WARN_PRINT_ONCE_ED("FSR1 3D scaling is only available when using the Forward+ renderer.");
			return;
		}
		if (p_mode == RSE::VIEWPORT_SCALING_3D_MODE_FSR2) {
			WARN_PRINT_ONCE_ED("FSR2 3D scaling is only available when using the Forward+ renderer.");
			return;
		}
		if (p_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_TEMPORAL) {
			WARN_PRINT_ONCE_ED("MetalFX Temporal 3D scaling is only available when using the Forward+ renderer.");
			return;
		}
	}
	if (rendering_method == "gl_compatibility" && p_mode == RSE::VIEWPORT_SCALING_3D_MODE_METALFX_SPATIAL) {
		WARN_PRINT_ONCE_ED("MetalFX Spatial 3D scaling is only available when using the Forward+ or Mobile renderer.");
		return;
	}

	if (viewport->scaling_3d_mode == p_mode) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->scaling_3d_mode = p_mode;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	if (viewport->dynamic.enabled) {
		// Only bilinear may exceed 1.0, and the costs differ per mode.
		viewport->dynamic.scale = CLAMP(viewport->dynamic.scale, viewport->dynamic.min_scale, _dynamic_scale_max(viewport));
		_dynamic_scale_restart(viewport, false);
	}

	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_fsr_sharpness(RID p_viewport, float p_sharpness) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->fsr_sharpness = p_sharpness;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_texture_mipmap_bias(RID p_viewport, float p_mipmap_bias) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_mipmap_bias = p_mipmap_bias;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_anisotropic_filtering_level(RID p_viewport, RSE::ViewportAnisotropicFiltering p_anisotropic_filtering_level) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->anisotropic_filtering_level = p_anisotropic_filtering_level;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_scaling_3d_scale(RID p_viewport, float p_scaling_3d_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	// Clamp to reasonable values that are actually useful.
	// Values above 2.0 don't serve a practical purpose since the viewport
	// isn't displayed with mipmaps.
	if (viewport->scaling_3d_scale == CLAMP(p_scaling_3d_scale, 0.1, 2.0)) {
		return;
	}

	viewport->scaling_3d_scale = CLAMP(p_scaling_3d_scale, 0.1, 2.0);
	if (viewport->dynamic.enabled) {
		// While dynamic, scaling_3d_scale is the starting scale: setting it restarts the controller from there (how a
		// project, or Viewport's remember option, puts back a scale it saved).
		viewport->dynamic.scale = CLAMP(viewport->scaling_3d_scale, viewport->dynamic.min_scale, _dynamic_scale_max(viewport));
		_dynamic_scale_restart(viewport, false);
	}
	_dynamic_scale_publish(viewport);
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_scaling_3d_dynamic(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	if (viewport->dynamic.enabled == p_enabled) {
		return;
	}
	viewport->dynamic.enabled = p_enabled;
	if (p_enabled && dynamic_viewport_count == 0) {
		dynamic_last_frame_usec = 0; // Not measured while no viewport needed it: start over.
		dynamic_frame_average_ms = 0.0;
	}
	dynamic_viewport_count += p_enabled ? 1 : -1;
	if (p_enabled) {
		viewport->dynamic.scale = CLAMP(viewport->scaling_3d_scale, viewport->dynamic.min_scale, _dynamic_scale_max(viewport));
		viewport->dynamic.changes = 0;
		_dynamic_scale_restart(viewport, true);
	} else {
		viewport->dynamic.state = RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_OFF;
		viewport->dynamic.source = RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_NONE;
		_dynamic_scale_publish(viewport);
	}
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_scaling_3d_dynamic_target_fps(RID p_viewport, float p_fps) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	ERR_FAIL_COND_MSG(p_fps < 1.0f, "The dynamic 3D resolution target must be at least 1 FPS.");
	viewport->dynamic.target_fps = p_fps;
	if (viewport->dynamic.enabled) {
		_dynamic_scale_restart(viewport, false);
	}
}

void RendererViewport::viewport_set_scaling_3d_dynamic_min_scale(RID p_viewport, float p_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->dynamic.min_scale = CLAMP(p_scale, 0.1f, 2.0f);
	if (viewport->dynamic.enabled) {
		const float clamped = CLAMP(viewport->dynamic.scale, viewport->dynamic.min_scale, _dynamic_scale_max(viewport));
		_dynamic_scale_restart(viewport, false);
		if (clamped != viewport->dynamic.scale) {
			viewport->dynamic.scale = clamped;
			_configure_3d_render_buffers(viewport);
		}
	}
}

void RendererViewport::viewport_set_scaling_3d_dynamic_max_scale(RID p_viewport, float p_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->dynamic.max_scale = CLAMP(p_scale, 0.1f, 2.0f);
	if (viewport->dynamic.enabled) {
		const float clamped = CLAMP(viewport->dynamic.scale, viewport->dynamic.min_scale, _dynamic_scale_max(viewport));
		_dynamic_scale_restart(viewport, false);
		// Also when the scale holds: FSR 2's context is sized from the maximum.
		viewport->dynamic.scale = clamped;
		_configure_3d_render_buffers(viewport);
	}
}

float RendererViewport::viewport_get_scaling_3d_effective_scale(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, 1.0);
	return viewport->dynamic.published_scale.load(std::memory_order_relaxed);
}

RSE::ViewportScaling3DDynamicState RendererViewport::viewport_get_scaling_3d_dynamic_state(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RSE::VIEWPORT_SCALING_3D_DYNAMIC_STATE_OFF);
	return RSE::ViewportScaling3DDynamicState(viewport->dynamic.published_state.load(std::memory_order_relaxed));
}

RSE::ViewportScaling3DDynamicSource RendererViewport::viewport_get_scaling_3d_dynamic_source(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RSE::VIEWPORT_SCALING_3D_DYNAMIC_SOURCE_NONE);
	return RSE::ViewportScaling3DDynamicSource(viewport->dynamic.published_source.load(std::memory_order_relaxed));
}

void RendererViewport::viewport_set_size(RID p_viewport, int p_width, int p_height, int p_view_count) {
	ERR_FAIL_COND(p_width < 0 || p_height < 0 || p_view_count < 0);

	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	_viewport_set_size(viewport, p_width, p_height, p_view_count);
}

void RendererViewport::_viewport_set_size(Viewport *p_viewport, int p_width, int p_height, uint32_t p_view_count) {
	Size2i new_size(p_width, p_height);
	if (p_viewport->size != new_size || p_viewport->view_count != p_view_count) {
		p_viewport->size = new_size;
		p_viewport->view_count = p_view_count;

		RSG::texture_storage->render_target_set_size(p_viewport->render_target, p_width, p_height, p_view_count);
		if (p_viewport->dynamic.enabled) {
			_dynamic_scale_restart(p_viewport, false); // Other pixel count: settle, and a new cost model.
		}
		_configure_3d_render_buffers(p_viewport);

		p_viewport->occlusion_buffer_dirty = true;
	}
}

bool RendererViewport::_viewport_requires_motion_vectors(Viewport *p_viewport) {
	return p_viewport->use_taa ||
			RSE::scaling_3d_mode_type(p_viewport->scaling_3d_mode) == RSE::VIEWPORT_SCALING_3D_TYPE_TEMPORAL ||
			p_viewport->debug_draw == RSE::VIEWPORT_DEBUG_DRAW_MOTION_VECTORS || p_viewport->force_motion_vectors;
}

void RendererViewport::viewport_set_active(RID p_viewport, bool p_active) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_active) {
		ERR_FAIL_COND_MSG(active_viewports.has(viewport), "Can't make active a Viewport that is already active.");
		viewport->occlusion_buffer_dirty = true;
		active_viewports.push_back(viewport);
	} else {
		active_viewports.erase(viewport);
	}

	sorted_active_viewports_dirty = true;
}

void RendererViewport::viewport_set_parent_viewport(RID p_viewport, RID p_parent_viewport) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->parent = p_parent_viewport;
}

void RendererViewport::viewport_set_clear_mode(RID p_viewport, RSE::ViewportClearMode p_clear_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->clear_mode = p_clear_mode;
}

void RendererViewport::viewport_attach_to_screen(RID p_viewport, const Rect2 &p_rect, DisplayServerEnums::WindowID p_screen) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_screen != DisplayServerEnums::INVALID_WINDOW_ID) {
		// If using OpenGL we can optimize this operation by rendering directly to system_fbo
		// instead of rendering to fbo and copying to system_fbo after
		if (RSG::rasterizer->is_low_end() && viewport->viewport_render_direct_to_screen) {
			RSG::texture_storage->render_target_set_size(viewport->render_target, p_rect.size.x, p_rect.size.y, viewport->view_count);
			RSG::texture_storage->render_target_set_position(viewport->render_target, p_rect.position.x, p_rect.position.y);
		}

		viewport->viewport_to_screen_rect = p_rect;
		viewport->viewport_to_screen = p_screen;
	} else {
		// if render_direct_to_screen was used, reset size and position
		if (RSG::rasterizer->is_low_end() && viewport->viewport_render_direct_to_screen) {
			RSG::texture_storage->render_target_set_position(viewport->render_target, 0, 0);
			RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->size.x, viewport->size.y, viewport->view_count);
		}

		viewport->viewport_to_screen_rect = Rect2();
		viewport->viewport_to_screen = DisplayServerEnums::INVALID_WINDOW_ID;
	}
}

void RendererViewport::viewport_set_render_direct_to_screen(RID p_viewport, bool p_enable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (p_enable == viewport->viewport_render_direct_to_screen) {
		return;
	}

	// if disabled, reset render_target size and position
	if (!p_enable) {
		RSG::texture_storage->render_target_set_position(viewport->render_target, 0, 0);
		RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->size.x, viewport->size.y, viewport->view_count);
	}

	RSG::texture_storage->render_target_set_direct_to_screen(viewport->render_target, p_enable);
	viewport->viewport_render_direct_to_screen = p_enable;

	// if attached to screen already, setup screen size and position, this needs to happen after setting flag to avoid an unnecessary buffer allocation
	if (RSG::rasterizer->is_low_end() && viewport->viewport_to_screen_rect != Rect2() && p_enable) {
		RSG::texture_storage->render_target_set_size(viewport->render_target, viewport->viewport_to_screen_rect.size.x, viewport->viewport_to_screen_rect.size.y, viewport->view_count);
		RSG::texture_storage->render_target_set_position(viewport->render_target, viewport->viewport_to_screen_rect.position.x, viewport->viewport_to_screen_rect.position.y);
	}
}

void RendererViewport::viewport_set_update_mode(RID p_viewport, RSE::ViewportUpdateMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->update_mode = p_mode;
}

RSE::ViewportUpdateMode RendererViewport::viewport_get_update_mode(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RSE::VIEWPORT_UPDATE_DISABLED);

	return viewport->update_mode;
}

RID RendererViewport::viewport_get_render_target(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	return viewport->render_target;
}

RID RendererViewport::viewport_get_texture(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	return RSG::texture_storage->render_target_get_texture(viewport->render_target);
}

RID RendererViewport::viewport_get_occluder_debug_texture(RID p_viewport) const {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, RID());

	if (viewport->use_occlusion_culling && viewport->debug_draw == RSE::VIEWPORT_DEBUG_DRAW_OCCLUDERS) {
		return RendererSceneOcclusionCull::get_singleton()->buffer_get_debug_texture(p_viewport);
	}
	return RID();
}

void RendererViewport::viewport_set_prev_camera_data(RID p_viewport, const RendererSceneRender::CameraData *p_camera_data) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	uint64_t frame = RSG::rasterizer->get_frame_number();
	if (viewport->prev_camera_data_frame != frame) {
		viewport->prev_camera_data = *p_camera_data;
		viewport->prev_camera_data_frame = frame;
	}
}

const RendererSceneRender::CameraData *RendererViewport::viewport_get_prev_camera_data(RID p_viewport) {
	const Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, nullptr);
	return &viewport->prev_camera_data;
}

void RendererViewport::viewport_set_disable_2d(RID p_viewport, bool p_disable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_2d = p_disable;
}

void RendererViewport::viewport_set_environment_mode(RID p_viewport, RSE::ViewportEnvironmentMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_environment = p_mode;
}

bool RendererViewport::viewport_is_environment_disabled(Viewport *viewport) {
	ERR_FAIL_NULL_V(viewport, false);

	if (viewport->parent.is_valid() && viewport->disable_environment == RSE::VIEWPORT_ENVIRONMENT_INHERIT) {
		Viewport *parent = viewport_owner.get_or_null(viewport->parent);
		return viewport_is_environment_disabled(parent);
	}
	return viewport->disable_environment == RSE::VIEWPORT_ENVIRONMENT_DISABLED;
}

void RendererViewport::viewport_set_disable_3d(RID p_viewport, bool p_disable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->disable_3d = p_disable;
}

void RendererViewport::viewport_attach_camera(RID p_viewport, RID p_camera) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->dynamic.enabled && viewport->camera != p_camera && !viewport->camera.is_valid()) {
		_dynamic_scale_restart(viewport, true); // 3D starts now: give it the startup period (shader compiles).
	}
	viewport->camera = p_camera;
}

void RendererViewport::viewport_set_scenario(RID p_viewport, RID p_scenario) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->scenario.is_valid()) {
		RSG::scene->scenario_remove_viewport_visibility_mask(viewport->scenario, p_viewport);
	}

	if (viewport->dynamic.enabled && viewport->scenario != p_scenario) {
		_dynamic_scale_restart(viewport, true); // A new scene: loading and shader compiles are not load.
	}
	viewport->scenario = p_scenario;
	if (viewport->use_occlusion_culling) {
		RendererSceneOcclusionCull::get_singleton()->buffer_set_scenario(p_viewport, p_scenario);
	}
}

void RendererViewport::viewport_attach_canvas(RID p_viewport, RID p_canvas) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(viewport->canvas_map.has(p_canvas));
	RendererCanvasCull::Canvas *canvas = RSG::canvas->canvas_owner.get_or_null(p_canvas);
	ERR_FAIL_NULL(canvas);

	canvas->viewports.insert(p_viewport);
	viewport->canvas_map[p_canvas] = Viewport::CanvasData();
	viewport->canvas_map[p_canvas].layer = 0;
	viewport->canvas_map[p_canvas].sublayer = 0;
	viewport->canvas_map[p_canvas].canvas = canvas;
}

void RendererViewport::viewport_remove_canvas(RID p_viewport, RID p_canvas) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RendererCanvasCull::Canvas *canvas = RSG::canvas->canvas_owner.get_or_null(p_canvas);
	ERR_FAIL_NULL(canvas);

	viewport->canvas_map.erase(p_canvas);
	canvas->viewports.erase(p_viewport);
}

void RendererViewport::viewport_set_canvas_transform(RID p_viewport, RID p_canvas, const Transform2D &p_offset) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(!viewport->canvas_map.has(p_canvas));
	viewport->canvas_map[p_canvas].transform = p_offset;
}

void RendererViewport::viewport_set_transparent_background(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	if (viewport->transparent_bg == p_enabled) {
		return;
	}

	RSG::texture_storage->render_target_set_transparent(viewport->render_target, p_enabled);
	viewport->transparent_bg = p_enabled;
}

void RendererViewport::viewport_set_global_canvas_transform(RID p_viewport, const Transform2D &p_transform) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->global_transform = p_transform;
}

void RendererViewport::viewport_set_canvas_stacking(RID p_viewport, RID p_canvas, int p_layer, int p_sublayer) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	ERR_FAIL_COND(!viewport->canvas_map.has(p_canvas));
	viewport->canvas_map[p_canvas].layer = p_layer;
	viewport->canvas_map[p_canvas].sublayer = p_sublayer;
}

void RendererViewport::viewport_set_positional_shadow_atlas_size(RID p_viewport, int p_size, bool p_16_bits) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->shadow_atlas_size = p_size;
	viewport->shadow_atlas_16_bits = p_16_bits;

	RSG::light_storage->shadow_atlas_set_size(viewport->shadow_atlas, viewport->shadow_atlas_size, viewport->shadow_atlas_16_bits);
}

void RendererViewport::viewport_set_positional_shadow_atlas_quadrant_subdivision(RID p_viewport, int p_quadrant, int p_subdiv) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::light_storage->shadow_atlas_set_quadrant_subdivision(viewport->shadow_atlas, p_quadrant, p_subdiv);
}

void RendererViewport::viewport_set_msaa_2d(RID p_viewport, RSE::ViewportMSAA p_msaa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->msaa_2d == p_msaa) {
		return;
	}
	viewport->msaa_2d = p_msaa;
	RSG::texture_storage->render_target_set_msaa(viewport->render_target, p_msaa);
}

void RendererViewport::viewport_set_msaa_3d(RID p_viewport, RSE::ViewportMSAA p_msaa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->msaa_3d == p_msaa) {
		return;
	}
	viewport->msaa_3d = p_msaa;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_hdr_2d(RID p_viewport, bool p_use_hdr_2d) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_hdr_2d == p_use_hdr_2d) {
		return;
	}
	viewport->use_hdr_2d = p_use_hdr_2d;
	RSG::texture_storage->render_target_set_use_hdr(viewport->render_target, p_use_hdr_2d);
	_configure_3d_render_buffers(viewport);
}

bool RendererViewport::viewport_is_using_hdr_2d(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, false);

	return viewport->use_hdr_2d;
}

void RendererViewport::viewport_set_screen_space_aa(RID p_viewport, RSE::ViewportScreenSpaceAA p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility" && p_mode != RSE::VIEWPORT_SCREEN_SPACE_AA_DISABLED) {
		WARN_PRINT_ONCE_ED("Screen-space AA is only available when using the Forward+ or Mobile renderer.");
		return;
	}

	if (viewport->screen_space_aa == p_mode) {
		return;
	}
	viewport->screen_space_aa = p_mode;
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_taa(RID p_viewport, bool p_use_taa) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	if (OS::get_singleton()->get_current_rendering_method() != "forward_plus") {
		WARN_PRINT_ONCE_ED("TAA is only available when using the Forward+ renderer.");
		return;
	}

	if (viewport->use_taa == p_use_taa) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->use_taa = p_use_taa;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_use_debanding(RID p_viewport, bool p_use_debanding) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_debanding == p_use_debanding) {
		return;
	}
	viewport->use_debanding = p_use_debanding;
	RSG::texture_storage->render_target_set_use_debanding(viewport->render_target, p_use_debanding);
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_force_motion_vectors(RID p_viewport, bool p_force_motion_vectors) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	_viewport_set_force_motion_vectors(viewport, p_force_motion_vectors);
}

void RendererViewport::_viewport_set_force_motion_vectors(RendererViewport::Viewport *p_viewport, bool p_force_motion_vectors) {
	if (p_viewport->force_motion_vectors == p_force_motion_vectors) {
		return;
	}

	bool motion_vectors_before = _viewport_requires_motion_vectors(p_viewport);
	p_viewport->force_motion_vectors = p_force_motion_vectors;

	bool motion_vectors_after = _viewport_requires_motion_vectors(p_viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}

	_configure_3d_render_buffers(p_viewport);
}

void RendererViewport::viewport_set_use_occlusion_culling(RID p_viewport, bool p_use_occlusion_culling) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	if (viewport->use_occlusion_culling == p_use_occlusion_culling) {
		return;
	}
	viewport->use_occlusion_culling = p_use_occlusion_culling;

	if (viewport->use_occlusion_culling) {
		RendererSceneOcclusionCull::get_singleton()->add_buffer(p_viewport);
		RendererSceneOcclusionCull::get_singleton()->buffer_set_scenario(p_viewport, viewport->scenario);
	} else {
		RendererSceneOcclusionCull::get_singleton()->remove_buffer(p_viewport);
	}

	viewport->occlusion_buffer_dirty = true;
}

void RendererViewport::viewport_set_occlusion_rays_per_thread(int p_rays_per_thread) {
	if (occlusion_rays_per_thread == p_rays_per_thread) {
		return;
	}

	occlusion_rays_per_thread = p_rays_per_thread;

	for (int i = 0; i < active_viewports.size(); i++) {
		active_viewports[i]->occlusion_buffer_dirty = true;
	}
}

void RendererViewport::viewport_set_occlusion_culling_build_quality(RSE::ViewportOcclusionCullingBuildQuality p_quality) {
	RendererSceneOcclusionCull::get_singleton()->set_build_quality(p_quality);
}

void RendererViewport::viewport_set_mesh_lod_threshold(RID p_viewport, float p_pixels) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->mesh_lod_threshold = p_pixels;
}

int RendererViewport::viewport_get_render_info(RID p_viewport, RSE::ViewportRenderInfoType p_type, RSE::ViewportRenderInfo p_info) {
	ERR_FAIL_INDEX_V(p_type, RSE::VIEWPORT_RENDER_INFO_TYPE_MAX, -1);
	ERR_FAIL_INDEX_V(p_info, RSE::VIEWPORT_RENDER_INFO_MAX, -1);

	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	if (!viewport) {
		return 0; //there should be a lock here..
	}

	return viewport->render_info.info[p_type][p_info];
}

void RendererViewport::viewport_set_debug_draw(RID p_viewport, RSE::ViewportDebugDraw p_draw) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	bool motion_vectors_before = _viewport_requires_motion_vectors(viewport);
	viewport->debug_draw = p_draw;

	bool motion_vectors_after = _viewport_requires_motion_vectors(viewport);
	if (motion_vectors_before != motion_vectors_after) {
		num_viewports_with_motion_vectors += motion_vectors_after ? 1 : -1;
	}
}

void RendererViewport::viewport_set_measure_render_time(RID p_viewport, bool p_enable) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->measure_render_time = p_enable;
}

float RendererViewport::viewport_get_measured_render_time_cpu(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, 0);

	return double(viewport->time_cpu_end - viewport->time_cpu_begin) / 1000.0;
}

float RendererViewport::viewport_get_measured_render_time_gpu(RID p_viewport) const {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL_V(viewport, 0);

	return double((viewport->time_gpu_end - viewport->time_gpu_begin) / 1000) / 1000.0;
}

void RendererViewport::viewport_set_snap_2d_transforms_to_pixel(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->snap_2d_transforms_to_pixel = p_enabled;
}

void RendererViewport::viewport_set_snap_2d_vertices_to_pixel(RID p_viewport, bool p_enabled) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->snap_2d_vertices_to_pixel = p_enabled;
}

void RendererViewport::viewport_set_default_canvas_item_texture_filter(RID p_viewport, RSE::CanvasItemTextureFilter p_filter) {
	ERR_FAIL_COND_MSG(p_filter == RSE::CANVAS_ITEM_TEXTURE_FILTER_DEFAULT, "Viewport does not accept DEFAULT as texture filter (it's the topmost choice already).)");
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_filter = p_filter;
}
void RendererViewport::viewport_set_default_canvas_item_texture_repeat(RID p_viewport, RSE::CanvasItemTextureRepeat p_repeat) {
	ERR_FAIL_COND_MSG(p_repeat == RSE::CANVAS_ITEM_TEXTURE_REPEAT_DEFAULT, "Viewport does not accept DEFAULT as texture repeat (it's the topmost choice already).)");
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	viewport->texture_repeat = p_repeat;
}

void RendererViewport::viewport_set_sdf_oversize_and_scale(RID p_viewport, RSE::ViewportSDFOversize p_size, RSE::ViewportSDFScale p_scale) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_sdf_size_and_scale(viewport->render_target, p_size, p_scale);
}

RID RendererViewport::viewport_find_from_screen_attachment(DisplayServerEnums::WindowID p_id) const {
	RID *rids = nullptr;
	uint32_t rid_count = viewport_owner.get_rid_count();
	rids = (RID *)alloca(sizeof(RID) * rid_count);
	viewport_owner.fill_owned_buffer(rids);
	for (uint32_t i = 0; i < rid_count; i++) {
		Viewport *viewport = viewport_owner.get_or_null(rids[i]);
		if (viewport->viewport_to_screen == p_id) {
			return rids[i];
		}
	}
	return RID();
}

void RendererViewport::viewport_set_vrs_mode(RID p_viewport, RSE::ViewportVRSMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_mode(viewport->render_target, p_mode);
	_configure_3d_render_buffers(viewport);
}

void RendererViewport::viewport_set_vrs_update_mode(RID p_viewport, RSE::ViewportVRSUpdateMode p_mode) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_update_mode(viewport->render_target, p_mode);
}

void RendererViewport::viewport_set_vrs_texture(RID p_viewport, RID p_texture) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);

	RSG::texture_storage->render_target_set_vrs_texture(viewport->render_target, p_texture);
	_configure_3d_render_buffers(viewport);
}

bool RendererViewport::free(RID p_rid) {
	if (viewport_owner.owns(p_rid)) {
		Viewport *viewport = viewport_owner.get_or_null(p_rid);

		RSG::texture_storage->render_target_free(viewport->render_target);
		RSG::light_storage->shadow_atlas_free(viewport->shadow_atlas);
		if (viewport->render_buffers.is_valid()) {
			viewport->render_buffers.unref();
		}

		while (viewport->canvas_map.begin()) {
			viewport_remove_canvas(p_rid, viewport->canvas_map.begin()->key);
		}

		viewport_set_scenario(p_rid, RID());
		active_viewports.erase(viewport);
		sorted_active_viewports_dirty = true;

		if (viewport->use_occlusion_culling) {
			RendererSceneOcclusionCull::get_singleton()->remove_buffer(p_rid);
		}

		if (_viewport_requires_motion_vectors(viewport)) {
			num_viewports_with_motion_vectors--;
		}

		if (viewport->dynamic.enabled) {
			dynamic_viewport_count--;
		}

		viewport_owner.free(p_rid);

		return true;
	}

	return false;
}

void RendererViewport::handle_timestamp(String p_timestamp, uint64_t p_cpu_time, uint64_t p_gpu_time) {
	RID *vp = timestamp_vp_map.getptr(p_timestamp);
	if (!vp) {
		return;
	}

	Viewport *viewport = viewport_owner.get_or_null(*vp);
	if (!viewport) {
		return;
	}

	if (p_timestamp.begins_with("vp_begin")) {
		viewport->time_cpu_begin = p_cpu_time;
		viewport->time_gpu_begin = p_gpu_time;
	}

	if (p_timestamp.begins_with("vp_end")) {
		viewport->time_cpu_end = p_cpu_time;
		viewport->time_gpu_end = p_gpu_time;
	}
}

void RendererViewport::viewport_set_canvas_cull_mask(RID p_viewport, uint32_t p_canvas_cull_mask) {
	Viewport *viewport = viewport_owner.get_or_null(p_viewport);
	ERR_FAIL_NULL(viewport);
	viewport->canvas_cull_mask = p_canvas_cull_mask;
}

// Workaround for setting this on thread.
void RendererViewport::call_set_vsync_mode(DisplayServerEnums::VSyncMode p_mode, DisplayServerEnums::WindowID p_window) {
	DisplayServer::get_singleton()->window_set_vsync_mode(p_mode, p_window);
}

int RendererViewport::get_total_objects_drawn() const {
	return total_objects_drawn;
}
int RendererViewport::get_total_primitives_drawn() const {
	return total_vertices_drawn;
}
int RendererViewport::get_total_draw_calls_used() const {
	return total_draw_calls_used;
}

int RendererViewport::get_num_viewports_with_motion_vectors() const {
	return num_viewports_with_motion_vectors;
}

RendererViewport::RendererViewport() {
	occlusion_rays_per_thread = GLOBAL_GET("rendering/occlusion_culling/occlusion_rays_per_thread");
}
