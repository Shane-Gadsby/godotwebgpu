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

#include "core/math/vector3.h"
#include "core/templates/rid_owner.h"
#include "servers/rendering/rendering_server_enums.h"

class RendererCameraAttributes {
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
	};

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

		return cam_attributes && (cam_attributes->dof_blur_near_enabled || cam_attributes->dof_blur_far_enabled) && cam_attributes->dof_blur_amount > 0.0;
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
