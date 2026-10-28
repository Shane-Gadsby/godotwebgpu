/**************************************************************************/
/*  camera_attributes.h                                                   */
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

#include "core/io/resource.h"
#include "core/templates/rid.h"
#include "scene/resources/texture.h"

class CameraAttributes : public Resource {
	GDCLASS(CameraAttributes, Resource);

public:
	enum GrainType {
		GRAIN_TYPE_FILM,
		GRAIN_TYPE_FILM_MONOCHROME,
		GRAIN_TYPE_DIGITAL,
	};

	enum LUTInput {
		LUT_INPUT_DISPLAY,
		LUT_INPUT_ARRI_LOGC3,
		LUT_INPUT_SONY_SLOG3,
		LUT_INPUT_PANASONIC_VLOG,
		LUT_INPUT_CANON_LOG3,
		LUT_INPUT_RED_LOG3G10,
		LUT_INPUT_ACESCCT,
		LUT_INPUT_MAX,
	};

	enum MeteringMode {
		METERING_MODE_AVERAGE,
		METERING_MODE_CENTER_WEIGHTED,
		METERING_MODE_SPOT,
		METERING_MODE_HIGHLIGHT_WEIGHTED,
	};

private:
	RID camera_attributes;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

	float exposure_multiplier = 1.0;
	float exposure_sensitivity = 100.0; // In ISO.
	void _update_exposure();

	bool auto_exposure_enabled = false;
	float auto_exposure_min = 0.01;
	float auto_exposure_max = 64.0;
	float auto_exposure_speed = 0.5;
	float auto_exposure_scale = 0.4;
	virtual void _update_auto_exposure() {}

	float white_balance_temperature = 6500.0; // In Kelvin.
	float white_balance_tint = 0.0;
	void _update_white_balance();

	bool grain_enabled = false;
	GrainType grain_type = GRAIN_TYPE_FILM;
	float grain_intensity = 1.0;
	float grain_size = 1.0;
	void _update_grain();

	// Camera finish: in-camera noise reduction and sharpening, after tonemapping.
	bool noise_reduction_enabled = false;
	float noise_reduction_strength = 1.0;
	bool noise_reduction_chroma_only = false;
	void _update_noise_reduction();
	bool sharpening_enabled = false;
	float sharpening_amount = 0.5;
	float sharpening_radius = 1.0;
	float sharpening_halo = 0.25;
	void _update_sharpening();

	bool lut_enabled = false;
	Ref<Texture3D> lut_texture;
	LUTInput lut_input = LUT_INPUT_DISPLAY;
	float lut_strength = 1.0;
	void _update_lut();

	bool motion_blur_enabled = false;
	float motion_blur_shutter_angle = 180.0; // In degrees.
	float motion_blur_strength = 1.0;
	float motion_blur_max_length = 0.05; // Fraction of the frame height.
	bool rolling_shutter_enabled = false;
	float rolling_shutter_readout_time = 20.0; // In milliseconds.
	bool motion_blur_visible_in_editor = false;
	bool rolling_shutter_visible_in_editor = false;
	bool _camera_motion_shows(bool p_visible_in_editor) const;
	void _update_motion_blur();
	void _update_rolling_shutter();
	// The exposure time motion blur uses, in seconds, or 0 to use the shutter angle.
	virtual float _get_motion_blur_shutter_time() const { return 0.0; }

	// Lens optics. CameraAttributesPhysical adds the physical lens they are derived from.
	bool vignetting_enabled = false;
	float vignetting_strength = 1.0;
	float vignetting_falloff = 2.0; // CameraAttributesPractical only.
	void _update_vignetting();

	bool distortion_enabled = false;
	bool distortion_automatic = true; // CameraAttributesPhysical only.
	float distortion_k1 = 0.0;
	float distortion_k2 = 0.0;
	bool distortion_auto_crop = true;
	void _update_distortion();

	bool chromatic_aberration_enabled = false;
	float chromatic_aberration_strength = 1.0;
	void _update_chromatic_aberration();

	// Exposure automation: auto exposure metering modes and auto white balance.
	MeteringMode auto_exposure_metering_mode = METERING_MODE_AVERAGE;
	float auto_exposure_metering_spot_size = 2.5; // Percent of the frame's area.
	Vector2 auto_exposure_metering_spot_point = Vector2(0.5, 0.5);
	float auto_exposure_metering_highlight_percentile = 99.0;
	bool auto_exposure_metering_log_average = false;
	void _update_auto_exposure_metering();
	bool white_balance_auto = false;
	float white_balance_auto_speed = 1.0;
	float white_balance_auto_min_temperature = 2500.0; // In Kelvin.
	float white_balance_auto_max_temperature = 10000.0;
	void _update_auto_white_balance();

	// Aperture, bokeh, starburst and halation (CameraAttributesPhysical adds tilt-shift).
	bool aperture_enabled = false;
	int aperture_blade_count = 6; // 0 = circular.
	float aperture_blade_curvature = 0.0;
	float aperture_rotation = 0.0; // In degrees.
	void _update_aperture();
	bool cats_eye_enabled = false;
	float cats_eye_strength = 1.0;
	void _update_cats_eye();
	bool starburst_enabled = false;
	float starburst_strength = 20.0;
	void _update_starburst();
	bool halation_enabled = false;
	float halation_strength = 1.0;
	float halation_radius = 1.0;
	bool halation_high_quality = false;
	void _update_halation();

	// Longitudinal chromatic aberration (bokeh fringing), in the DOF pass.
	bool longitudinal_chromatic_aberration_enabled = false;
	float longitudinal_chromatic_aberration_strength = 1.0;
	void _update_longitudinal_chromatic_aberration();

public:
	virtual RID get_rid() const override;
	virtual float calculate_exposure_normalization() const { return 1.0; }

	void set_exposure_multiplier(float p_multiplier);
	float get_exposure_multiplier() const;
	void set_exposure_sensitivity(float p_sensitivity);
	float get_exposure_sensitivity() const;

	void set_auto_exposure_enabled(bool p_enabled);
	bool is_auto_exposure_enabled() const;
	void set_auto_exposure_speed(float p_auto_exposure_speed);
	float get_auto_exposure_speed() const;
	void set_auto_exposure_scale(float p_auto_exposure_scale);
	float get_auto_exposure_scale() const;

	void set_white_balance_temperature(float p_temperature);
	float get_white_balance_temperature() const;
	void set_white_balance_tint(float p_tint);
	float get_white_balance_tint() const;

	void set_auto_exposure_metering_mode(MeteringMode p_mode);
	MeteringMode get_auto_exposure_metering_mode() const;
	void set_auto_exposure_metering_spot_size(float p_size);
	float get_auto_exposure_metering_spot_size() const;
	void set_auto_exposure_metering_spot_point(const Vector2 &p_point);
	Vector2 get_auto_exposure_metering_spot_point() const;
	void set_auto_exposure_metering_highlight_percentile(float p_percentile);
	float get_auto_exposure_metering_highlight_percentile() const;
	void set_auto_exposure_metering_log_average(bool p_enabled);
	bool is_auto_exposure_metering_log_average() const;

	void set_white_balance_auto(bool p_enabled);
	bool is_white_balance_auto() const;
	void set_white_balance_auto_speed(float p_speed);
	float get_white_balance_auto_speed() const;
	void set_white_balance_auto_min_temperature(float p_temperature);
	float get_white_balance_auto_min_temperature() const;
	void set_white_balance_auto_max_temperature(float p_temperature);
	float get_white_balance_auto_max_temperature() const;

	void set_grain_enabled(bool p_enabled);
	bool is_grain_enabled() const;
	void set_grain_type(GrainType p_type);
	GrainType get_grain_type() const;
	void set_grain_intensity(float p_intensity);
	float get_grain_intensity() const;
	void set_grain_size(float p_size);
	float get_grain_size() const;

	void set_noise_reduction_enabled(bool p_enabled);
	bool is_noise_reduction_enabled() const;
	void set_noise_reduction_strength(float p_strength);
	float get_noise_reduction_strength() const;
	void set_noise_reduction_chroma_only(bool p_chroma_only);
	bool is_noise_reduction_chroma_only() const;

	void set_sharpening_enabled(bool p_enabled);
	bool is_sharpening_enabled() const;
	void set_sharpening_amount(float p_amount);
	float get_sharpening_amount() const;
	void set_sharpening_radius(float p_radius);
	float get_sharpening_radius() const;
	void set_sharpening_halo(float p_halo);
	float get_sharpening_halo() const;

	void set_lut_enabled(bool p_enabled);
	bool is_lut_enabled() const;
	void set_lut_texture(const Ref<Texture3D> &p_texture);
	Ref<Texture3D> get_lut_texture() const;
	void set_lut_input(LUTInput p_input);
	LUTInput get_lut_input() const;
	void set_lut_strength(float p_strength);
	float get_lut_strength() const;

	void set_motion_blur_enabled(bool p_enabled);
	bool is_motion_blur_enabled() const;
	void set_motion_blur_shutter_angle(float p_angle);
	float get_motion_blur_shutter_angle() const;
	void set_motion_blur_strength(float p_strength);
	float get_motion_blur_strength() const;
	void set_motion_blur_max_length(float p_length);
	float get_motion_blur_max_length() const;

	void set_rolling_shutter_enabled(bool p_enabled);
	bool is_rolling_shutter_enabled() const;
	void set_rolling_shutter_readout_time(float p_time);
	float get_rolling_shutter_readout_time() const;
	void set_motion_blur_visible_in_editor(bool p_visible);
	bool is_motion_blur_visible_in_editor() const;
	void set_rolling_shutter_visible_in_editor(bool p_visible);
	bool is_rolling_shutter_visible_in_editor() const;

	void set_vignetting_enabled(bool p_enabled);
	bool is_vignetting_enabled() const;
	void set_vignetting_strength(float p_strength);
	float get_vignetting_strength() const;
	void set_vignetting_falloff(float p_falloff);
	float get_vignetting_falloff() const;

	void set_distortion_enabled(bool p_enabled);
	bool is_distortion_enabled() const;
	void set_distortion_automatic(bool p_automatic);
	bool is_distortion_automatic() const;
	void set_distortion_k1(float p_k1);
	float get_distortion_k1() const;
	void set_distortion_k2(float p_k2);
	float get_distortion_k2() const;
	void set_distortion_auto_crop(bool p_auto_crop);
	bool is_distortion_auto_crop() const;

	void set_chromatic_aberration_enabled(bool p_enabled);
	bool is_chromatic_aberration_enabled() const;
	void set_chromatic_aberration_strength(float p_strength);
	float get_chromatic_aberration_strength() const;

	void set_aperture_enabled(bool p_value);
	bool is_aperture_enabled() const;
	void set_aperture_blade_count(int p_value);
	int get_aperture_blade_count() const;
	void set_aperture_blade_curvature(float p_value);
	float get_aperture_blade_curvature() const;
	void set_aperture_rotation(float p_value);
	float get_aperture_rotation() const;
	void set_cats_eye_enabled(bool p_value);
	bool is_cats_eye_enabled() const;
	void set_cats_eye_strength(float p_value);
	float get_cats_eye_strength() const;
	void set_starburst_enabled(bool p_value);
	bool is_starburst_enabled() const;
	void set_starburst_strength(float p_value);
	float get_starburst_strength() const;
	void set_halation_enabled(bool p_value);
	bool is_halation_enabled() const;
	void set_halation_strength(float p_value);
	float get_halation_strength() const;
	void set_halation_radius(float p_value);
	float get_halation_radius() const;
	void set_halation_high_quality(bool p_enabled);
	bool is_halation_high_quality() const;
	void set_longitudinal_chromatic_aberration_enabled(bool p_value);
	bool is_longitudinal_chromatic_aberration_enabled() const;
	void set_longitudinal_chromatic_aberration_strength(float p_value);
	float get_longitudinal_chromatic_aberration_strength() const;

	CameraAttributes();
	~CameraAttributes();
};

class CameraAttributesPractical : public CameraAttributes {
	GDCLASS(CameraAttributesPractical, CameraAttributes);

private:
	// DOF blur
	bool dof_blur_far_enabled = false;
	float dof_blur_far_distance = 10.0;
	float dof_blur_far_transition = 5.0;

	bool dof_blur_near_enabled = false;
	float dof_blur_near_distance = 2.0;
	float dof_blur_near_transition = 1.0;

	float dof_blur_amount = 0.1;
	void _update_dof_blur();

	virtual void _update_auto_exposure() override;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	// DOF blur
	void set_dof_blur_far_enabled(bool p_enabled);
	bool is_dof_blur_far_enabled() const;
	void set_dof_blur_far_distance(float p_distance);
	float get_dof_blur_far_distance() const;
	void set_dof_blur_far_transition(float p_distance);
	float get_dof_blur_far_transition() const;

	void set_dof_blur_near_enabled(bool p_enabled);
	bool is_dof_blur_near_enabled() const;
	void set_dof_blur_near_distance(float p_distance);
	float get_dof_blur_near_distance() const;
	void set_dof_blur_near_transition(float p_distance);
	float get_dof_blur_near_transition() const;
	void set_dof_blur_amount(float p_amount);
	float get_dof_blur_amount() const;

	void set_auto_exposure_min_sensitivity(float p_min);
	float get_auto_exposure_min_sensitivity() const;
	void set_auto_exposure_max_sensitivity(float p_max);
	float get_auto_exposure_max_sensitivity() const;

	virtual float calculate_exposure_normalization() const override;

	CameraAttributesPractical();
	~CameraAttributesPractical();
};

class CameraAttributesPhysical : public CameraAttributes {
	GDCLASS(CameraAttributesPhysical, CameraAttributes);

public:
	enum SensorFormat {
		SENSOR_FORMAT_FULL_FRAME,
		SENSOR_FORMAT_APS_C,
		SENSOR_FORMAT_SUPER_35,
		SENSOR_FORMAT_MICRO_FOUR_THIRDS,
		SENSOR_FORMAT_MEDIUM_FORMAT,
		SENSOR_FORMAT_ONE_INCH,
		SENSOR_FORMAT_CUSTOM,
		SENSOR_FORMAT_MAX,
	};

	static Vector2 get_sensor_format_size(SensorFormat p_format);

private:
	// Sensor and lens.
	SensorFormat sensor_format = SENSOR_FORMAT_FULL_FRAME;
	Vector2 sensor_size = Vector2(36.0, 24.0); // In millimeters.
	float lens_max_aperture = 1.8; // In f-stops.
	float lens_transmission_loss = 0.0; // In stops: the T-stop is the f-stop times 2^(loss / 2).
	float lens_anamorphic_squeeze = 1.0;
	float lens_focus_breathing = 0.0;
	float exposure_nd_filter = 0.0; // In stops.
	bool diffraction_enabled = false;
	float diffraction_strength = 1.0;
	float _get_fov_focal_length() const;
	void _update_lens();
	void _update_diffraction();

	// Tilt-shift: the lens tilted against the sensor, so the plane of focus tilts (Scheimpflug).
	bool tilt_shift_enabled = false;
	float tilt_shift_tilt = 0.0; // In degrees.
	float tilt_shift_axis = 0.0; // In degrees.
	void _update_tilt_shift();

	// Exposure
	float exposure_aperture = 16.0; // In f-stops;
	float exposure_shutter_speed = 100.0; // In 1 / seconds;

	bool motion_blur_use_shutter_speed = true;
	virtual float _get_motion_blur_shutter_time() const override;

	// Camera properties.
	float frustum_focal_length = 35.0; // In millimeters.
	float frustum_focus_distance = 10.0; // In Meters.
	real_t frustum_near = 0.05;
	real_t frustum_far = 4000.0;
	real_t frustum_fov = 75.0;
	void _update_frustum();

	virtual void _update_auto_exposure() override;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &property) const;

public:
	void set_aperture(float p_aperture);
	float get_aperture() const;

	void set_shutter_speed(float p_shutter_speed);
	float get_shutter_speed() const;

	void set_motion_blur_use_shutter_speed(bool p_enable);
	bool is_motion_blur_using_shutter_speed() const;

	void set_focal_length(float p_focal_length);
	float get_focal_length() const;

	void set_focus_distance(float p_focus_distance);
	float get_focus_distance() const;

	void set_near(real_t p_near);
	real_t get_near() const;

	void set_far(real_t p_far);
	real_t get_far() const;

	real_t get_fov() const;
	// True when get_fov() is the horizontal field of view (an anamorphic lens), false when it is the vertical one.
	bool is_fov_horizontal() const;

	void set_sensor_format(SensorFormat p_format);
	SensorFormat get_sensor_format() const;
	void set_sensor_size(const Vector2 &p_size);
	Vector2 get_sensor_size() const;

	void set_lens_max_aperture(float p_aperture);
	float get_lens_max_aperture() const;
	void set_lens_transmission_loss(float p_stops);
	float get_lens_transmission_loss() const;
	void set_lens_anamorphic_squeeze(float p_squeeze);
	float get_lens_anamorphic_squeeze() const;
	void set_lens_focus_breathing(float p_amount);
	float get_lens_focus_breathing() const;

	void set_exposure_nd_filter(float p_stops);
	float get_exposure_nd_filter() const;

	void set_diffraction_enabled(bool p_enabled);
	bool is_diffraction_enabled() const;
	void set_diffraction_strength(float p_strength);
	float get_diffraction_strength() const;

	void set_tilt_shift_enabled(bool p_enabled);
	bool is_tilt_shift_enabled() const;
	void set_tilt_shift_tilt(float p_degrees);
	float get_tilt_shift_tilt() const;
	void set_tilt_shift_axis(float p_degrees);
	float get_tilt_shift_axis() const;

	void set_auto_exposure_min_exposure_value(float p_min);
	float get_auto_exposure_min_exposure_value() const;
	void set_auto_exposure_max_exposure_value(float p_max);
	float get_auto_exposure_max_exposure_value() const;

	virtual float calculate_exposure_normalization() const override;

	CameraAttributesPhysical();
	~CameraAttributesPhysical();
};

VARIANT_ENUM_CAST(CameraAttributes::GrainType);
VARIANT_ENUM_CAST(CameraAttributes::MeteringMode);
VARIANT_ENUM_CAST(CameraAttributes::LUTInput);
VARIANT_ENUM_CAST(CameraAttributesPhysical::SensorFormat);
