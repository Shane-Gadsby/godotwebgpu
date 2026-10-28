/**************************************************************************/
/*  camera_local_exposure.h                                               */
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

#include "servers/rendering/renderer_rd/shaders/effects/camera_local_exposure.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/storage/camera_attributes_storage.h"

#define RB_CAMERA_LOCAL_EXPOSURE_BUFFERS SNAME("camera_local_exposure_buffers")

namespace RendererRD {

// CameraAttributes local exposure: builds the bilateral grid of the frame's log luminance that the Forward+ tonemap
// pass slices to expose each pixel by its surroundings (camera_local_exposure_inc.glsl). Two compute passes on a grid
// of about (W / 64) x (H / 64) x 33 cells: MODE_BUILD sums the frame into it, MODE_BLUR blurs it. Forward+ only. See
// camera_local_exposure.glsl for the passes and camera_attributes_storage.cpp ("Local exposure") for the model.
class CameraLocalExposure {
public:
	class LocalExposureBuffers : public RenderBufferCustomDataRD {
		GDCLASS(LocalExposureBuffers, RenderBufferCustomDataRD);

	public:
		Size2i grid_size; // Cells across and down; the textures are this x (GRID_DEPTH + 1).
		RID raw_grid; // RGBA32F 3D: the sums MODE_BUILD writes.
		RID grid; // RGBA16F 3D: the blurred grid the tonemapper samples.

		virtual void configure(RenderSceneBuffersRD *p_render_buffers) override {}
		virtual void free_data() override;
	};

	struct Settings {
		RendererCameraAttributes::LocalExposure local_exposure;
		// How the tonemapper exposes the frame, so the grid holds the luminance it will see.
		float exposure = 1.0;
		float luminance_multiplier = 1.0;
		bool use_auto_exposure = false;
		RID auto_exposure_texture;
		float auto_exposure_scale = 1.0;
	};

	CameraLocalExposure();
	~CameraLocalExposure();

	Ref<LocalExposureBuffers> get_buffers(Ref<RenderSceneBuffersRD> p_render_buffers);

	// Builds the grid from p_source (p_source_size pixels, the texture the tonemap pass reads); returns the texture the
	// tonemapper samples.
	RID build_grid(RID p_source, const Size2i &p_source_size, Ref<LocalExposureBuffers> p_buffers, const Settings &p_settings);

private:
	enum Mode {
		MODE_BUILD,
		MODE_BLUR,
		MODE_MAX
	};

	struct PushConstant {
		int32_t source_size[2];
		int32_t grid_size[2];

		float cell_size;
		float exposure;
		float auto_exposure_scale;
		float luminance_multiplier;

		float blur_sigma;
		int32_t blur_radius;
		uint32_t flags;
		float pad;
	};
	static_assert(sizeof(PushConstant) == 48, "Must match the push constant of camera_local_exposure.glsl.");

	enum {
		FLAG_USE_AUTO_EXPOSURE = 1,
	};

	CameraLocalExposureShaderRD shader;
	RID shader_version;
	RID pipelines[MODE_MAX];
};

} // namespace RendererRD
