/**************************************************************************/
/*  camera_metering.h                                                     */
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

#include "servers/rendering/renderer_rd/effects/luminance.h"
#include "servers/rendering/renderer_rd/shaders/effects/camera_metering.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/storage/camera_attributes_storage.h"

#define RB_CAMERA_METERING_BUFFERS SNAME("camera_metering_buffers")

namespace RendererRD {

// CameraAttributes exposure automation on the GPU: the auto exposure metering modes other than Average, which write
// the same 1x1 luminance texture Luminance's reduction does (so the tonemapper, glow and everything else that reads
// it are unchanged), and auto white balance, whose gains the tonemapper reads from a 1x1 state texture. Forward+ only:
// both are compute reductions. See camera_metering.glsl for the passes and camera_attributes_storage.cpp for the model.
class CameraMetering {
public:
	class MeteringBuffers : public RenderBufferCustomDataRD {
		GDCLASS(MeteringBuffers, RenderBufferCustomDataRD);

	public:
		// The sums chain, RGBA32F: each level 1/8 of the one before, rounded up, down to 1x1. Shared by the weighted
		// metering and auto white balance, which run one after the other.
		Vector<RID> sums;
		Vector<Size2i> sums_sizes;
		RID histogram; // HISTOGRAM_BINS uints, created on first use.
		RID awb_state[2]; // 2x1 RGBA32F; awb_state[awb_current] is the latest.
		int awb_current = 0;
		uint64_t awb_version = 0;

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override;
		virtual void free_data() override;
	};

	struct ExposureSettings {
		RendererCameraAttributes::AutoExposureMetering metering;
		float min_luminance = 0.0;
		float max_luminance = 0.0;
		float adjust = 0.0;
		bool set = false; // Start over without adaptation, as Luminance::luminance_reduction()'s p_set.
	};

	struct WhiteBalanceSettings {
		RendererCameraAttributes::AutoWhiteBalance awb;
		double time_step = 0.0;
		// How the tonemapper exposes the frame, to reject pixels that clip.
		float exposure = 1.0;
		float luminance_multiplier = 1.0;
		bool use_auto_exposure = false;
		RID auto_exposure_texture;
		float auto_exposure_scale = 1.0;
	};

	CameraMetering();
	~CameraMetering();

	Ref<MeteringBuffers> get_buffers(Ref<RenderSceneBuffersRD> p_render_buffers);

	// Meters p_source with p_settings' mode into p_luminance's current texture, in place of
	// Luminance::luminance_reduction(). Not for METERING_AVERAGE.
	void meter_exposure(RID p_source, const Size2i &p_source_size, Ref<Luminance::LuminanceBuffers> p_luminance, Ref<MeteringBuffers> p_buffers, const ExposureSettings &p_settings);
	// Updates the auto white balance state from p_source; returns the state texture the tonemapper reads its gains from.
	RID auto_white_balance(RID p_source, const Size2i &p_source_size, Ref<MeteringBuffers> p_buffers, const WhiteBalanceSettings &p_settings);

private:
	enum Mode {
		MODE_METER_READ,
		MODE_REDUCE,
		MODE_METER_RESOLVE,
		MODE_HISTOGRAM,
		MODE_HISTOGRAM_RESOLVE,
		MODE_AWB_READ,
		MODE_AWB_RESOLVE,
		MODE_MAX
	};

	static constexpr uint32_t HISTOGRAM_BINS = 256;

	struct PushConstant {
		int32_t source_size[2];
		float adjust;
		uint32_t flags;

		float min_luminance;
		float max_luminance;
		float highlight_percentile;
		float highlight_key;

		float center[2];
		float radius;
		float inner_weight;

		float outer_weight;
		float exposure;
		float auto_exposure_scale;
		float luminance_multiplier;

		float min_temperature;
		float max_temperature;
		float pad[2];
	};
	static_assert(sizeof(PushConstant) == 80, "Must match the push constant of camera_metering.glsl.");

	enum {
		FLAG_SET = 1,
		FLAG_USE_AUTO_EXPOSURE = 2,
		FLAG_LOG_AVERAGE = 4, // Weighted log-average instead of the weighted mean (the percentile is unaffected).
	};

	CameraMeteringShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];

	// Dispatches p_read_mode (with its inputs in p_read_set0) over the source, then MODE_REDUCE down the sums chain to
	// 1x1, in sums[sums.size() - 1].
	void _reduce(RD::ComputeListID p_compute_list, Mode p_read_mode, RID p_read_set0, const Size2i &p_source_size, Ref<MeteringBuffers> p_buffers, PushConstant &r_push_constant);
};

} // namespace RendererRD
