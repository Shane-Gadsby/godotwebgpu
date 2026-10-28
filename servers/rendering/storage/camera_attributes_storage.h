/**************************************************************************/
/*  camera_attributes_storage.h                                           */
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

#include "core/math/vector2.h"
#include "core/math/vector2i.h"
#include "core/math/vector3.h"
#include "core/templates/rid_owner.h"
#include "servers/rendering/rendering_server_enums.h"

// The CameraEffects uniform buffer of the tonemap pass, for CameraAttributes effects whose parameters do not fit the
// push constant (the Forward+ one is full at 128 bytes). std140; the shaders declare the same layout in
// servers/rendering/renderer_rd/shaders/effects/camera_effects_inc.glsl and its Compatibility copy, so keep the three in
// step. Each region belongs to one group of effects, and its header word says which of them run:
// - header[0], lens[8]: lens optics (camera_lens_inc.glsl): distortion, chromatic aberration, diffraction, vignetting.
// - header[1], glare[6]: glare (camera_glare_inc.glsl): lens flare, veiling glare, diffusion filters, starbursts and
//   halation, all read from the glare chain (a mip chain of the scene).
// - header[2], exposure[2]: reserved for exposure automation (task 36).
// - header[3], reserved[15]: free; grow a region into it rather than reordering.
// An all-zero header means no effect runs.
struct CameraEffectsUBO {
	uint32_t header[4] = {};
	float lens[8][4] = {};
	float glare[6][4] = {};
	float exposure[2][4] = {};
	float reserved[15][4] = {};
};
static_assert(sizeof(CameraEffectsUBO) == 512, "CameraEffectsUBO must match the 512-byte std140 block in camera_effects_inc.glsl.");

// The Environment's glare settings (scene look: lens flare, veiling glare, diffusion filter), copied in by the renderer
// so the camera attributes storage needs no access to the environment storage.
struct CameraGlareEnvironment {
	bool lens_flare_enabled = false;
	float lens_flare_intensity = 1.0;
	float lens_flare_threshold = 1.0;
	int lens_flare_ghost_count = 4;
	float lens_flare_halo_intensity = 1.0;
	float lens_flare_chromatic_aberration = 1.0;
	bool veiling_glare_enabled = false;
	float veiling_glare_strength = 1.0;
	bool diffusion_filter_enabled = false;
	float diffusion_filter_strength = 0.25;
	bool diffusion_filter_black = false;
};

// What a tonemap pass knows about itself when it fills the buffer. Effects that need more (the Environment for glare,
// the previous frame for exposure) add members here rather than parameters to the fill function.
struct CameraEffectsContext {
	Size2i source_size; // The texture the pass reads, in pixels: lens resampling works in its texels.
	Size2i output_size; // The pixels the pass writes.
	uint64_t frame = 0;
	// Glare: the Environment's settings and the glare chain the renderer built for this pass (zero size when none).
	CameraGlareEnvironment glare_environment;
	Size2i glare_chain_size; // Mip 0 of the glare chain, in texels.
	int glare_chain_mipmaps = 0;
	float glare_chain_sigma = 1.0; // Blur sigma of the chain's lod L is this times 2^L, in texels of lod 0 (measured).
};

class RendererCameraAttributes {
public:
	// Bits of CameraEffectsUBO::header[0]; camera_lens_inc.glsl has the same values.
	enum LensBits : uint32_t {
		LENS_DISTORTION = 1,
		LENS_CHROMATIC_ABERRATION = 2,
		LENS_DIFFRACTION = 4,
		LENS_VIGNETTING_PHYSICAL = 8,
		LENS_VIGNETTING_PRACTICAL = 16,
	};

	// Bits of CameraEffectsUBO::header[1]; camera_glare_inc.glsl has the same values.
	enum GlareBits : uint32_t {
		GLARE_LENS_FLARE = 1,
		GLARE_VEILING = 2,
		GLARE_DIFFUSION = 4,
		GLARE_DIFFUSION_BLACK = 8,
		GLARE_STARBURST = 16,
		GLARE_HALATION = 32,
	};

private:
	static RendererCameraAttributes *singleton;

	struct CameraAttributes {
		float exposure_multiplier = 1.0;
		float exposure_normalization = 1.0;
		float exposure_sensitivity = 100.0; // In ISO.

		bool use_auto_exposure = false;
		float auto_exposure_min_sensitivity = 50.0;
		float auto_exposure_max_sensitivity = 800.0;
		float auto_exposure_adjust_speed = 1.0;
		float auto_exposure_scale = 1.0;
		uint64_t auto_exposure_version = 0;

		bool dof_blur_far_enabled = false;
		float dof_blur_far_distance = 10;
		float dof_blur_far_transition = 5;
		bool dof_blur_near_enabled = false;
		float dof_blur_near_distance = 2;
		float dof_blur_near_transition = 1;
		float dof_blur_amount = 0.1;

		// White balance. The defaults are neutral: the tonemapper skips it entirely then.
		float white_balance_temperature = 6500.0; // In Kelvin.
		float white_balance_tint = 0.0;
		bool use_white_balance = false;
		Vector3 white_balance_gains = Vector3(1.0, 1.0, 1.0); // Cone-space (Bradford LMS) gains.

		// Camera finish: in-camera noise reduction and sharpening, applied after tonemapping by the camera finish
		// pass. Off by default; the pass is skipped entirely then.
		bool noise_reduction_enabled = false;
		float noise_reduction_strength = 1.0;
		bool noise_reduction_chroma_only = false;
		bool sharpening_enabled = false;
		float sharpening_amount = 0.5;
		float sharpening_radius = 1.0; // In pixels of a 1080-line frame.
		float sharpening_halo = 0.25;

		// Grain. Off by default; the tonemappers skip it entirely then.
		bool grain_enabled = false;
		RSE::CameraAttributesGrainType grain_type = RSE::CAMERA_ATTRIBUTES_GRAIN_FILM;
		float grain_intensity = 1.0;
		float grain_size = 1.0;
		float grain_sensitivity = 100.0; // In ISO.

		// LUT. Off by default. The texture is a Texture3D's RID, resolved by the renderers.
		bool lut_enabled = false;
		RID lut_texture;
		RSE::CameraAttributesLUTInput lut_input = RSE::CAMERA_ATTRIBUTES_LUT_INPUT_DISPLAY;
		float lut_strength = 1.0;

		// Motion blur and rolling shutter. Off by default; the renderer skips their pass then.
		bool motion_blur_enabled = false;
		float motion_blur_shutter_angle = 180.0; // In degrees.
		float motion_blur_shutter_time = 0.0; // In seconds; when positive, it is used instead of the angle.
		float motion_blur_strength = 1.0;
		float motion_blur_max_length = 0.05; // Fraction of the frame height.
		bool rolling_shutter_enabled = false;
		float rolling_shutter_readout_time = 20.0; // In milliseconds.

		// Lens optics. The physical lens comes from CameraAttributesPhysical; a focal length of 0 means there is
		// none (CameraAttributesPractical), and only the frame-relative effects apply.
		Vector2 sensor_size = Vector2(36.0, 24.0); // In millimeters.
		float lens_focal_length = 0.0; // Nominal, in millimeters.
		float lens_fov_focal_length = 0.0; // The one that sets the field of view, with focus breathing.
		float lens_focus_distance = 10.0; // In meters.
		float lens_f_number = 16.0;
		float lens_max_f_number = 1.8;
		float lens_anamorphic_squeeze = 1.0;
		bool vignetting_enabled = false;
		float vignetting_strength = 1.0;
		float vignetting_falloff = 2.0;
		bool distortion_enabled = false;
		bool distortion_automatic = false;
		float distortion_k1 = 0.0;
		float distortion_k2 = 0.0;
		bool distortion_auto_crop = true;
		bool chromatic_aberration_enabled = false;
		float chromatic_aberration_strength = 1.0;
		bool diffraction_enabled = false;
		float diffraction_strength = 1.0;

		// Aperture, bokeh, starburst and halation. Off by default: the DOF keeps the project's bokeh shape, and the
		// glare chain is not built.
		bool aperture_enabled = false;
		int aperture_blade_count = 6; // 0 = circular.
		float aperture_blade_curvature = 0.0; // 0 = straight blades, 1 = a round opening.
		float aperture_rotation = 0.0; // In degrees.
		bool cats_eye_enabled = false;
		float cats_eye_strength = 1.0;
		bool tilt_shift_enabled = false; // CameraAttributesPhysical only (it needs the focal length).
		float tilt_shift_tilt = 0.0; // Lens tilt, in degrees.
		float tilt_shift_axis = 0.0; // Direction of the tilt axis in the frame, in degrees (0 = horizontal).
		bool starburst_enabled = false;
		float starburst_strength = 1.0;
		bool halation_enabled = false;
		float halation_strength = 1.0;
		float halation_radius = 1.0;
	};

	static uint32_t _lens_requested_bits(const CameraAttributes &p_attributes);
	static void _fill_effects_lens(const CameraAttributes &p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo);
	static void _fill_effects_glare(const CameraAttributes *p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo);
	static uint32_t _glare_requested_bits(const CameraAttributes *p_attributes, const CameraGlareEnvironment &p_environment);
	static void _fill_effects_exposure(const CameraAttributes &p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo);

	RSE::DOFBlurQuality dof_blur_quality = RSE::DOF_BLUR_QUALITY_MEDIUM;
	RSE::DOFBokehShape dof_blur_bokeh_shape = RSE::DOF_BOKEH_HEXAGON;
	bool dof_blur_use_jitter = false;
	RSE::CameraAttributesMotionBlurQuality motion_blur_quality = RSE::CAMERA_ATTRIBUTES_MOTION_BLUR_QUALITY_MEDIUM;
	static uint64_t auto_exposure_counter;

	mutable RID_Owner<CameraAttributes, true> camera_attributes_owner;

public:
	static RendererCameraAttributes *get_singleton() { return singleton; }

	RendererCameraAttributes();
	~RendererCameraAttributes();

	CameraAttributes *get_camera_attributes(RID p_rid) { return camera_attributes_owner.get_or_null(p_rid); }
	bool owns_camera_attributes(RID p_rid) { return camera_attributes_owner.owns(p_rid); }

	RID camera_attributes_allocate();
	void camera_attributes_initialize(RID p_rid);
	void camera_attributes_free(RID p_rid);

	void camera_attributes_set_dof_blur_quality(RSE::DOFBlurQuality p_quality, bool p_use_jitter);
	void camera_attributes_set_dof_blur_bokeh_shape(RSE::DOFBokehShape p_shape);

	void camera_attributes_set_dof_blur(RID p_camera_attributes, bool p_far_enable, float p_far_distance, float p_far_transition, bool p_near_enable, float p_near_distance, float p_near_transition, float p_amount);
	bool camera_attributes_get_dof_far_enabled(RID p_camera_attributes);
	float camera_attributes_get_dof_far_distance(RID p_camera_attributes);
	float camera_attributes_get_dof_far_transition(RID p_camera_attributes);
	bool camera_attributes_get_dof_near_enabled(RID p_camera_attributes);
	float camera_attributes_get_dof_near_distance(RID p_camera_attributes);
	float camera_attributes_get_dof_near_transition(RID p_camera_attributes);
	float camera_attributes_get_dof_blur_amount(RID p_camera_attributes);

	_FORCE_INLINE_ bool camera_attributes_uses_dof(RID p_camera_attributes) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);

		// Tilt-shift tilts the plane of focus, so the DOF runs even where the hyperfocal test turned both sides off.
		return cam_attributes && (cam_attributes->dof_blur_near_enabled || cam_attributes->dof_blur_far_enabled || (cam_attributes->tilt_shift_enabled && cam_attributes->tilt_shift_tilt != 0.0f && cam_attributes->lens_focal_length > 0.0f)) && cam_attributes->dof_blur_amount > 0.0;
	}

	void camera_attributes_set_exposure(RID p_camera_attributes, float p_multiplier, float p_exposure_normalization);
	float camera_attributes_get_exposure_normalization_factor(RID p_camera_attributes);

	void camera_attributes_set_auto_exposure(RID p_camera_attributes, bool p_enable, float p_min_sensitivity, float p_max_sensitivity, float p_speed, float p_scale);
	float camera_attributes_get_auto_exposure_min_sensitivity(RID p_camera_attributes);
	float camera_attributes_get_auto_exposure_max_sensitivity(RID p_camera_attributes);
	float camera_attributes_get_auto_exposure_adjust_speed(RID p_camera_attributes);
	float camera_attributes_get_auto_exposure_scale(RID p_camera_attributes);
	uint64_t camera_attributes_get_auto_exposure_version(RID p_camera_attributes);

	_FORCE_INLINE_ bool camera_attributes_uses_auto_exposure(RID p_camera_attributes) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);

		return cam_attributes && cam_attributes->use_auto_exposure;
	}

	void camera_attributes_set_white_balance(RID p_camera_attributes, float p_temperature, float p_tint);
	float camera_attributes_get_white_balance_temperature(RID p_camera_attributes);
	float camera_attributes_get_white_balance_tint(RID p_camera_attributes);
	// The von Kries gains the tonemappers apply in Bradford cone space (see the .cpp for the model).
	Vector3 camera_attributes_get_white_balance_gains(RID p_camera_attributes);
	static Vector3 white_balance_compute_gains(float p_temperature, float p_tint);

	_FORCE_INLINE_ bool camera_attributes_uses_white_balance(RID p_camera_attributes) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);

		return cam_attributes && cam_attributes->use_white_balance;
	}

	/* Camera finish */

	struct CameraFinishSettings {
		// Noise reduction: a bilateral filter in display-encoded Y'CbCr (see the .cpp).
		bool noise_reduction = false;
		float nr_spatial_sigma_px = 0.0; // Gaussian sigma of the spatial kernel, in output pixels.
		float nr_range_sigma = 0.0; // Gaussian sigma of the range kernel on Y', in display code values (0-1).
		bool nr_chroma_only = false;
		// Sharpening: an unsharp mask with a halo limit.
		bool sharpening = false;
		float sharpen_amount = 0.0;
		float sharpen_sigma_px = 0.0; // Gaussian sigma of the blur that is subtracted, in output pixels.
		float sharpen_halo = 1.0; // Allowed overshoot past the 3x3 neighborhood's range, as a fraction of it.
	};

	void camera_attributes_set_noise_reduction(RID p_camera_attributes, bool p_enable, float p_strength, bool p_chroma_only);
	void camera_attributes_set_sharpening(RID p_camera_attributes, bool p_enable, float p_amount, float p_radius, float p_halo);
	// What the camera finish pass does for an output p_output_height pixels tall; all off when it has nothing to do.
	CameraFinishSettings camera_attributes_get_finish(RID p_camera_attributes, int p_output_height);

	void camera_attributes_set_grain(RID p_camera_attributes, bool p_enable, RSE::CameraAttributesGrainType p_type, float p_intensity, float p_size, float p_sensitivity);
	// The grain parameters for a tonemap pass whose output is p_output_height pixels tall, packed the way
	// camera_grain_inc.glsl reads them, or 0 when there is no grain (see the .cpp for the model).
	uint32_t camera_attributes_get_grain_packed(RID p_camera_attributes, int p_output_height, uint64_t p_frame);

	void camera_attributes_set_lut(RID p_camera_attributes, bool p_enable, RID p_texture, RSE::CameraAttributesLUTInput p_input, float p_strength);
	RID camera_attributes_get_lut_texture(RID p_camera_attributes);
	// The LUT parameters packed the way camera_lut_inc.glsl reads them, or 0 when there is no LUT. The renderer
	// still has to check that the texture resolves to a 3D texture.
	uint32_t camera_attributes_get_lut_packed(RID p_camera_attributes);

	_FORCE_INLINE_ bool camera_attributes_uses_lut(RID p_camera_attributes) {
		return camera_attributes_get_lut_packed(p_camera_attributes) != 0;
	}

	/* Motion blur and rolling shutter */

	// What a frame's shutter does with the motion recorded in the velocity buffer (see the .cpp for the model). All
	// three are multiples of one frame's motion, so the renderer only scales the velocity it reads.
	struct ShutterMotion {
		float blur_scale = 0.0; // Blur length per unit of a frame's motion: exposure time / frame time * strength.
		float max_length = 0.0; // Longest blur, as a fraction of the frame height.
		float rolling_shutter = 0.0; // Readout time from the top row to the bottom one, in frames.
	};

	void camera_attributes_set_motion_blur_quality(RSE::CameraAttributesMotionBlurQuality p_quality);
	void camera_attributes_set_motion_blur(RID p_camera_attributes, bool p_enable, float p_shutter_angle, float p_shutter_time, float p_strength, float p_max_length);
	void camera_attributes_set_rolling_shutter(RID p_camera_attributes, bool p_enable, float p_readout_time);
	ShutterMotion camera_attributes_get_shutter_motion(RID p_camera_attributes, double p_frame_time);
	// Samples along the blur per pixel, from ProjectSettings rendering/camera/motion_blur/motion_blur_quality.
	uint32_t camera_attributes_get_motion_blur_sample_count() const;

	_FORCE_INLINE_ bool camera_attributes_uses_shutter_motion(RID p_camera_attributes) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);

		return cam_attributes && ((cam_attributes->motion_blur_enabled && cam_attributes->motion_blur_strength > 0.0f && cam_attributes->motion_blur_max_length > 0.0f) || (cam_attributes->rolling_shutter_enabled && cam_attributes->rolling_shutter_readout_time > 0.0f));
	}

	_FORCE_INLINE_ bool camera_attributes_uses_grain(RID p_camera_attributes) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);

		return cam_attributes && cam_attributes->grain_enabled && cam_attributes->grain_intensity > 0.0f;
	}

	/* Lens optics */

	void camera_attributes_set_lens(RID p_camera_attributes, const Vector2 &p_sensor_size, float p_focal_length, float p_fov_focal_length, float p_focus_distance, float p_f_number, float p_max_f_number, float p_anamorphic_squeeze);
	void camera_attributes_set_vignetting(RID p_camera_attributes, bool p_enable, float p_strength, float p_falloff);
	void camera_attributes_set_lens_distortion(RID p_camera_attributes, bool p_enable, bool p_automatic, float p_k1, float p_k2, bool p_auto_crop);
	void camera_attributes_set_chromatic_aberration(RID p_camera_attributes, bool p_enable, float p_strength);
	void camera_attributes_set_diffraction(RID p_camera_attributes, bool p_enable, float p_strength);
	// The typical corner distortion of a rectilinear lens whose frame corner is at p_tan_corner (tangent of the field
	// angle), as a relative radial displacement (negative = barrel). See the .cpp for the source.
	static double lens_typical_corner_distortion(double p_tan_corner);

	/* CameraEffects uniform buffer */

	/* Aperture, bokeh, tilt-shift, starburst and halation */

	// What the DOF pass needs to shape its bokeh, worked out from the attributes (see the .cpp for the models).
	struct BokehShape {
		bool custom = false; // False: the project's bokeh shape, unchanged.
		uint32_t blade_count = 0; // 0 = circular.
		float blade_curvature = 0.0;
		float rotation = 0.0; // In radians.
		float squeeze = 1.0; // Anamorphic squeeze: the bokeh is this many times taller than wide.
		// Cat's eye: the bokeh is the overlap of the aperture (unit circle) with the barrel's circle of this radius,
		// offset toward the frame's center by cats_eye_offset times the position in frame units.
		float cats_eye_offset = 0.0;
		float cats_eye_radius = 1.0;
		Vector2 frame_half_extent = Vector2(1.0, 1.0); // Frame half extents in frame units (corner at |q| = 1).
		// Tilt-shift: the plane of sharp focus, in view space (normal, unit), through (0, 0, -focus distance).
		bool tilt = false;
		Vector3 focus_plane_normal = Vector3(0.0, 0.0, 1.0);
		float focal_length = 0.0; // In meters.
	};

	void camera_attributes_set_aperture(RID p_camera_attributes, bool p_enable, int p_blade_count, float p_blade_curvature, float p_rotation);
	void camera_attributes_set_cats_eye(RID p_camera_attributes, bool p_enable, float p_strength);
	void camera_attributes_set_tilt_shift(RID p_camera_attributes, bool p_enable, float p_tilt, float p_axis);
	void camera_attributes_set_starburst(RID p_camera_attributes, bool p_enable, float p_strength);
	void camera_attributes_set_halation(RID p_camera_attributes, bool p_enable, float p_strength, float p_radius);
	// The bokeh for a DOF pass whose frame is p_aspect wide per unit of height.
	BokehShape camera_attributes_get_bokeh_shape(RID p_camera_attributes, float p_aspect);
	// Tilt-shift makes every pixel's focus distance differ, so the DOF must run even where the untilted lens would be
	// sharp everywhere.
	bool camera_attributes_uses_tilt_shift(RID p_camera_attributes);
	// Whether the tonemap pass needs the glare chain: the camera's starburst or halation, or the Environment's glare.
	bool camera_attributes_uses_glare(RID p_camera_attributes, const CameraGlareEnvironment &p_environment);

	_FORCE_INLINE_ bool camera_attributes_uses_effects_buffer(RID p_camera_attributes, const CameraGlareEnvironment &p_environment = CameraGlareEnvironment()) {
		CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
		return (cam_attributes && _lens_requested_bits(*cam_attributes) != 0) || _glare_requested_bits(cam_attributes, p_environment) != 0;
	}
	// Fills the buffer for one tonemap pass. Returns false, with the buffer all zeros, when no effect uses it: the
	// renderers then bind a shared zero buffer, and the output is bit-identical to having no buffer at all.
	bool camera_attributes_fill_effects_buffer(RID p_camera_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo);

	_FORCE_INLINE_ RSE::DOFBlurQuality camera_attributes_get_dof_blur_quality() {
		return dof_blur_quality;
	}

	_FORCE_INLINE_ RSE::DOFBokehShape camera_attributes_get_dof_blur_bokeh_shape() {
		return dof_blur_bokeh_shape;
	}

	_FORCE_INLINE_ bool camera_attributes_get_dof_blur_use_jitter() {
		return dof_blur_use_jitter;
	}
};
