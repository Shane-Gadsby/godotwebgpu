/**************************************************************************/
/*  bokeh_dof.h                                                           */
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
#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/bokeh_dof.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/bokeh_dof_raster.glsl.gen.h"

namespace RendererRD {

class BokehDOF {
private:
	bool prefer_raster_effects;

	struct BokehPushConstant {
		uint32_t size[2];
		float z_far;
		float z_near;

		uint32_t orthogonal;
		float blur_size;
		float blur_scale;
		uint32_t steps;

		uint32_t blur_near_active;
		float blur_near_begin;
		float blur_near_end;
		uint32_t blur_far_active;

		float blur_far_begin;
		float blur_far_end;
		uint32_t second_pass;
		uint32_t half_size;

		uint32_t use_jitter;
		float jitter_seed;
		uint32_t use_physical_near;
		uint32_t use_physical_far;

		float blur_size_near;
		float blur_size_far;

		// The aperture's shape, cat's eye and tilt-shift (bokeh_dof_inc.glsl reads them; half floats are packed in
		// pairs, low half first).
		uint32_t shape_flags; // Bits 0-7: blade count (0 = circular); BOKEH_SHAPE_* above them.
		uint32_t blade_shape; // Blade curvature, rotation (radians).
		uint32_t cats_eye; // Barrel offset per frame unit, barrel radius (both in aperture radii).
		uint32_t frame_half_extent; // Frame half extents in frame units.
		uint32_t squeeze_focal_length; // Anamorphic squeeze, focal length (meters).
		uint32_t view_ray_scale; // View ray x / -z, y / -z per NDC unit.
		uint32_t view_ray_offset; // ...and at NDC 0 (an off-center projection).
		float tilt_normal_x; // The plane of focus's normal in view space; z is positive.
		float tilt_normal_y;
		uint32_t longitudinal_ca; // Red and blue blur offset, scale (half floats).
	};
	static_assert(sizeof(BokehPushConstant) == 128, "The bokeh push constant must stay within Vulkan's guaranteed 128 bytes.");

	enum BokehShapeFlags : uint32_t {
		BOKEH_SHAPE_CUSTOM = 1 << 8,
		BOKEH_SHAPE_CATS_EYE = 1 << 9,
		BOKEH_SHAPE_TILT = 1 << 10,
		BOKEH_LONGITUDINAL_CA = 1 << 11,
		BOKEH_PHYSICAL_SIZE = 1 << 12, // CameraAttributesPhysical.dof_physical_blur_size (bokeh_physical_size_resolve()).
	};

	// Sets the longitudinal chromatic aberration's flag and red/blue blur offsets from the camera attributes.
	void _setup_longitudinal_ca(RID p_camera_attributes);
	// CameraAttributesPhysical.dof_physical_blur_size: the lens's blur radius in pixels of p_size, and how far the gather
	// reaches. Leaves both alone and returns false when the camera does not size its blur physically.
	static bool _physical_blur_size(RID p_camera_attributes, const Size2i &p_size, float &r_blur_size, float &r_reach);

	// Fills the push constant's shape fields from the camera attributes; returns whether the bokeh has a custom
	// shape, which only the circular gather can draw.
	bool _setup_bokeh_shape(RID p_camera_attributes, const Size2i &p_size, const Projection &p_projection, bool p_orthogonal);

	enum BokehMode {
		BOKEH_GEN_BLUR_SIZE,
		BOKEH_GEN_BOKEH_BOX,
		BOKEH_GEN_BOKEH_BOX_NOWEIGHT,
		BOKEH_GEN_BOKEH_HEXAGONAL,
		BOKEH_GEN_BOKEH_HEXAGONAL_NOWEIGHT,
		BOKEH_GEN_BOKEH_CIRCULAR,
		BOKEH_COMPOSITE,
		// Same as BOKEH_GEN_BLUR_SIZE, but for RenderSceneBuffersRD::get_depth_texture()'s
		// MSAA-resolve-target form -- a plain R32Float storage texture with real depth
		// VALUES (populated by Resolve::resolve_depth()/resolve_gi(), see Task 24 round 2/3
		// in webgpu_notes/TASKS.md), not a native hardware depth FORMAT. The two forms
		// need genuinely different WGSL on WebGPU (texture_depth_2d vs texture_2d<f32> --
		// this driver's depth-texture reclassification heuristic, Task 7.13, can't tell
		// them apart from GLSL source alone, since both are read identically), so this is
		// a real second shader variant, not just a parameter -- selected by
		// bokeh_dof_compute()'s new p_msaa_resolved_depth based on whether MSAA 3D is
		// active for the viewport being blurred. See Task 24 round 4.
		BOKEH_GEN_BLUR_SIZE_RESOLVED_DEPTH,
		BOKEH_MAX
	};

	struct Bokeh {
		BokehPushConstant push_constant;
		BokehDofShaderRD compute_shader;
		BokehDofRasterShaderRD raster_shader;
		RID shader_version;
		PipelineDeferredRD compute_pipelines[BOKEH_MAX];
		PipelineCacheRD raster_pipelines[BOKEH_MAX];
	} bokeh;

public:
	struct BokehBuffers {
		// bokeh buffers

		// textures
		Size2i base_texture_size;
		RID base_texture;
		RID depth_texture;
		RID secondary_texture;
		RID half_texture[2];

		// raster only
		RID base_fb;
		RID secondary_fb; // with weights
		RID half_fb[2]; // with weights
		RID base_weight_fb;
		RID weight_texture[4];
	};

	BokehDOF(bool p_prefer_raster_effects);
	~BokehDOF();

	void bokeh_dof_compute(const BokehBuffers &p_buffers, RID p_camera_attributes, float p_cam_znear, float p_cam_zfar, bool p_cam_orthogonal, const Projection &p_cam_projection, bool p_msaa_resolved_depth = false);
	void bokeh_dof_raster(const BokehBuffers &p_buffers, RID p_camera_attributes, float p_cam_znear, float p_cam_zfar, bool p_cam_orthogonal, const Projection &p_cam_projection);
};

} // namespace RendererRD
