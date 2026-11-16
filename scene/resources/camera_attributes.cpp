/**************************************************************************/
/*  camera_attributes.cpp                                                 */
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

#include "camera_attributes.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "servers/rendering/rendering_server.h"

void CameraAttributes::set_exposure_multiplier(float p_multiplier) {
	exposure_multiplier = p_multiplier;
	_update_exposure();
	emit_changed();
}

float CameraAttributes::get_exposure_multiplier() const {
	return exposure_multiplier;
}

void CameraAttributes::set_exposure_sensitivity(float p_sensitivity) {
	exposure_sensitivity = p_sensitivity;
	_update_exposure();
	_update_grain(); // Faster film and higher sensor gain are grainier.
	emit_changed();
}

float CameraAttributes::get_exposure_sensitivity() const {
	return exposure_sensitivity;
}

void CameraAttributes::_update_exposure() {
	float exposure_normalization = 1.0;
	// Ignore physical properties if not using physical light units.
	if (GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/use_physical_light_units")) {
		exposure_normalization = calculate_exposure_normalization();
	}

	RS::get_singleton()->camera_attributes_set_exposure(camera_attributes, exposure_multiplier, exposure_normalization);
}

void CameraAttributes::set_auto_exposure_enabled(bool p_enabled) {
	auto_exposure_enabled = p_enabled;
	_update_auto_exposure();
	notify_property_list_changed();
}

bool CameraAttributes::is_auto_exposure_enabled() const {
	return auto_exposure_enabled;
}

void CameraAttributes::set_auto_exposure_speed(float p_auto_exposure_speed) {
	auto_exposure_speed = p_auto_exposure_speed;
	_update_auto_exposure();
}

float CameraAttributes::get_auto_exposure_speed() const {
	return auto_exposure_speed;
}

void CameraAttributes::set_auto_exposure_scale(float p_auto_exposure_scale) {
	auto_exposure_scale = p_auto_exposure_scale;
	_update_auto_exposure();
}

float CameraAttributes::get_auto_exposure_scale() const {
	return auto_exposure_scale;
}

void CameraAttributes::set_white_balance_temperature(float p_temperature) {
	white_balance_temperature = p_temperature;
	_update_white_balance();
	emit_changed();
}

float CameraAttributes::get_white_balance_temperature() const {
	return white_balance_temperature;
}

void CameraAttributes::set_white_balance_tint(float p_tint) {
	white_balance_tint = p_tint;
	_update_white_balance();
	emit_changed();
}

float CameraAttributes::get_white_balance_tint() const {
	return white_balance_tint;
}

/* Exposure automation */

void CameraAttributes::set_auto_exposure_metering_mode(MeteringMode p_mode) {
	auto_exposure_metering_mode = p_mode;
	_update_auto_exposure_metering();
	notify_property_list_changed();
}

CameraAttributes::MeteringMode CameraAttributes::get_auto_exposure_metering_mode() const {
	return auto_exposure_metering_mode;
}

void CameraAttributes::set_auto_exposure_metering_spot_size(float p_size) {
	auto_exposure_metering_spot_size = p_size;
	_update_auto_exposure_metering();
}

float CameraAttributes::get_auto_exposure_metering_spot_size() const {
	return auto_exposure_metering_spot_size;
}

void CameraAttributes::set_auto_exposure_metering_spot_point(const Vector2 &p_point) {
	auto_exposure_metering_spot_point = p_point;
	_update_auto_exposure_metering();
}

Vector2 CameraAttributes::get_auto_exposure_metering_spot_point() const {
	return auto_exposure_metering_spot_point;
}

void CameraAttributes::set_auto_exposure_metering_highlight_percentile(float p_percentile) {
	auto_exposure_metering_highlight_percentile = p_percentile;
	_update_auto_exposure_metering();
}

float CameraAttributes::get_auto_exposure_metering_highlight_percentile() const {
	return auto_exposure_metering_highlight_percentile;
}

void CameraAttributes::_update_auto_exposure_metering() {
	RS::get_singleton()->camera_attributes_set_auto_exposure_metering(camera_attributes, RSE::CameraAttributesMeteringMode(auto_exposure_metering_mode), auto_exposure_metering_spot_size, auto_exposure_metering_spot_point, auto_exposure_metering_highlight_percentile);
	emit_changed();
}

void CameraAttributes::set_auto_exposure_metering_log_average(bool p_enabled) {
	auto_exposure_metering_log_average = p_enabled;
	RS::get_singleton()->camera_attributes_set_auto_exposure_metering_log_average(camera_attributes, auto_exposure_metering_log_average);
	emit_changed();
}

bool CameraAttributes::is_auto_exposure_metering_log_average() const {
	return auto_exposure_metering_log_average;
}

void CameraAttributes::set_white_balance_auto(bool p_enabled) {
	white_balance_auto = p_enabled;
	_update_auto_white_balance();
	notify_property_list_changed();
}

bool CameraAttributes::is_white_balance_auto() const {
	return white_balance_auto;
}

void CameraAttributes::set_white_balance_auto_speed(float p_speed) {
	white_balance_auto_speed = p_speed;
	_update_auto_white_balance();
}

float CameraAttributes::get_white_balance_auto_speed() const {
	return white_balance_auto_speed;
}

void CameraAttributes::set_white_balance_auto_min_temperature(float p_temperature) {
	white_balance_auto_min_temperature = p_temperature;
	_update_auto_white_balance();
}

float CameraAttributes::get_white_balance_auto_min_temperature() const {
	return white_balance_auto_min_temperature;
}

void CameraAttributes::set_white_balance_auto_max_temperature(float p_temperature) {
	white_balance_auto_max_temperature = p_temperature;
	_update_auto_white_balance();
}

float CameraAttributes::get_white_balance_auto_max_temperature() const {
	return white_balance_auto_max_temperature;
}

void CameraAttributes::_update_auto_white_balance() {
	RS::get_singleton()->camera_attributes_set_auto_white_balance(camera_attributes, white_balance_auto, white_balance_auto_speed, white_balance_auto_min_temperature, white_balance_auto_max_temperature);
	emit_changed();
}

/* Local exposure */

void CameraAttributes::set_local_exposure_enabled(bool p_enabled) {
	local_exposure_enabled = p_enabled;
	_update_local_exposure();
	notify_property_list_changed();
}

bool CameraAttributes::is_local_exposure_enabled() const {
	return local_exposure_enabled;
}

void CameraAttributes::set_local_exposure_highlight_contrast(float p_contrast) {
	local_exposure_highlight_contrast = CLAMP(p_contrast, 0.0f, 2.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_highlight_contrast() const {
	return local_exposure_highlight_contrast;
}

void CameraAttributes::set_local_exposure_shadow_contrast(float p_contrast) {
	local_exposure_shadow_contrast = CLAMP(p_contrast, 0.0f, 2.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_shadow_contrast() const {
	return local_exposure_shadow_contrast;
}

void CameraAttributes::set_local_exposure_detail_strength(float p_strength) {
	local_exposure_detail_strength = CLAMP(p_strength, 0.0f, 4.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_detail_strength() const {
	return local_exposure_detail_strength;
}

void CameraAttributes::set_local_exposure_middle_gray_bias(float p_bias) {
	local_exposure_middle_gray_bias = CLAMP(p_bias, -15.0f, 15.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_middle_gray_bias() const {
	return local_exposure_middle_gray_bias;
}

void CameraAttributes::set_local_exposure_blurred_luminance_blend(float p_blend) {
	local_exposure_blurred_luminance_blend = CLAMP(p_blend, 0.0f, 1.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_blurred_luminance_blend() const {
	return local_exposure_blurred_luminance_blend;
}

void CameraAttributes::set_local_exposure_blurred_luminance_kernel_size(float p_size) {
	local_exposure_blurred_luminance_kernel_size = CLAMP(p_size, 1.0f, 100.0f);
	_update_local_exposure();
}

float CameraAttributes::get_local_exposure_blurred_luminance_kernel_size() const {
	return local_exposure_blurred_luminance_kernel_size;
}

void CameraAttributes::_update_local_exposure() {
	RS::get_singleton()->camera_attributes_set_local_exposure(camera_attributes, local_exposure_enabled, local_exposure_highlight_contrast, local_exposure_shadow_contrast, local_exposure_detail_strength, local_exposure_middle_gray_bias);
	RS::get_singleton()->camera_attributes_set_local_exposure_blurred_luminance(camera_attributes, local_exposure_blurred_luminance_blend, local_exposure_blurred_luminance_kernel_size);
	emit_changed();
}

void CameraAttributes::_update_white_balance() {
	RS::get_singleton()->camera_attributes_set_white_balance(camera_attributes, white_balance_temperature, white_balance_tint);
}

void CameraAttributes::set_grain_enabled(bool p_enabled) {
	grain_enabled = p_enabled;
	_update_grain();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_grain_enabled() const {
	return grain_enabled;
}

void CameraAttributes::set_grain_type(GrainType p_type) {
	ERR_FAIL_INDEX(p_type, 3);
	grain_type = p_type;
	_update_grain();
	emit_changed();
}

CameraAttributes::GrainType CameraAttributes::get_grain_type() const {
	return grain_type;
}

void CameraAttributes::set_grain_intensity(float p_intensity) {
	grain_intensity = p_intensity;
	_update_grain();
	emit_changed();
}

float CameraAttributes::get_grain_intensity() const {
	return grain_intensity;
}

void CameraAttributes::set_grain_size(float p_size) {
	grain_size = p_size;
	_update_grain();
	emit_changed();
}

float CameraAttributes::get_grain_size() const {
	return grain_size;
}

void CameraAttributes::_update_grain() {
	RS::get_singleton()->camera_attributes_set_grain(camera_attributes, grain_enabled, RSE::CameraAttributesGrainType(grain_type), grain_intensity, grain_size, exposure_sensitivity);
}

/* Camera finish (see RendererCameraAttributes for the models) */

void CameraAttributes::set_noise_reduction_enabled(bool p_enabled) {
	noise_reduction_enabled = p_enabled;
	_update_noise_reduction();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_noise_reduction_enabled() const {
	return noise_reduction_enabled;
}

void CameraAttributes::set_noise_reduction_strength(float p_strength) {
	noise_reduction_strength = MAX(p_strength, 0.0f);
	_update_noise_reduction();
	emit_changed();
}

float CameraAttributes::get_noise_reduction_strength() const {
	return noise_reduction_strength;
}

void CameraAttributes::set_noise_reduction_chroma_only(bool p_chroma_only) {
	noise_reduction_chroma_only = p_chroma_only;
	_update_noise_reduction();
	emit_changed();
}

bool CameraAttributes::is_noise_reduction_chroma_only() const {
	return noise_reduction_chroma_only;
}

void CameraAttributes::_update_noise_reduction() {
	RS::get_singleton()->camera_attributes_set_noise_reduction(camera_attributes, noise_reduction_enabled, noise_reduction_strength, noise_reduction_chroma_only);
}

void CameraAttributes::set_sharpening_enabled(bool p_enabled) {
	sharpening_enabled = p_enabled;
	_update_sharpening();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_sharpening_enabled() const {
	return sharpening_enabled;
}

void CameraAttributes::set_sharpening_amount(float p_amount) {
	sharpening_amount = MAX(p_amount, 0.0f);
	_update_sharpening();
	emit_changed();
}

float CameraAttributes::get_sharpening_amount() const {
	return sharpening_amount;
}

void CameraAttributes::set_sharpening_radius(float p_radius) {
	sharpening_radius = MAX(p_radius, 0.01f);
	_update_sharpening();
	emit_changed();
}

float CameraAttributes::get_sharpening_radius() const {
	return sharpening_radius;
}

void CameraAttributes::set_sharpening_halo(float p_halo) {
	sharpening_halo = MAX(p_halo, 0.0f);
	_update_sharpening();
	emit_changed();
}

float CameraAttributes::get_sharpening_halo() const {
	return sharpening_halo;
}

void CameraAttributes::_update_sharpening() {
	RS::get_singleton()->camera_attributes_set_sharpening(camera_attributes, sharpening_enabled, sharpening_amount, sharpening_radius, sharpening_halo);
}

void CameraAttributes::set_lut_enabled(bool p_enabled) {
	lut_enabled = p_enabled;
	_update_lut();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_lut_enabled() const {
	return lut_enabled;
}

void CameraAttributes::set_lut_texture(const Ref<Texture3D> &p_texture) {
	lut_texture = p_texture;
	_update_lut();
	emit_changed();
}

Ref<Texture3D> CameraAttributes::get_lut_texture() const {
	return lut_texture;
}

void CameraAttributes::set_lut_input(LUTInput p_input) {
	ERR_FAIL_INDEX(p_input, LUT_INPUT_MAX);
	lut_input = p_input;
	_update_lut();
	emit_changed();
}

CameraAttributes::LUTInput CameraAttributes::get_lut_input() const {
	return lut_input;
}

void CameraAttributes::set_lut_strength(float p_strength) {
	lut_strength = CLAMP(p_strength, 0.0f, 1.0f);
	_update_lut();
	emit_changed();
}

float CameraAttributes::get_lut_strength() const {
	return lut_strength;
}

void CameraAttributes::_update_lut() {
	RS::get_singleton()->camera_attributes_set_lut(camera_attributes, lut_enabled, lut_texture.is_valid() ? lut_texture->get_rid() : RID(), RSE::CameraAttributesLUTInput(lut_input), lut_strength);
}

void CameraAttributes::set_motion_blur_enabled(bool p_enabled) {
	motion_blur_enabled = p_enabled;
	_update_motion_blur();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_motion_blur_enabled() const {
	return motion_blur_enabled;
}

void CameraAttributes::set_shutter_angle(float p_angle) {
	motion_blur_shutter_angle = p_angle;
	_update_motion_blur();
	_update_exposure(); // CameraAttributesPhysical's Angle mode exposes for it.
	emit_changed();
}

float CameraAttributes::get_shutter_angle() const {
	return motion_blur_shutter_angle;
}

void CameraAttributes::set_motion_blur_shutter_angle(float p_angle) {
	set_shutter_angle(p_angle);
}

float CameraAttributes::get_motion_blur_shutter_angle() const {
	return motion_blur_shutter_angle;
}

void CameraAttributes::set_motion_blur_strength(float p_strength) {
	motion_blur_strength = p_strength;
	_update_motion_blur();
	emit_changed();
}

float CameraAttributes::get_motion_blur_strength() const {
	return motion_blur_strength;
}

void CameraAttributes::set_motion_blur_max_length(float p_length) {
	motion_blur_max_length = p_length;
	_update_motion_blur();
	emit_changed();
}

float CameraAttributes::get_motion_blur_max_length() const {
	return motion_blur_max_length;
}

// Motion blur and rolling shutter in the editor: they come from the camera's motion, so in the editor they smear and
// skew the 3D viewport whenever the user navigates it, which hides what is being edited. Unlike DOF or grain they are
// therefore off in the editor unless the user ticks Visible in Editor; a running project always gets them.
bool CameraAttributes::_camera_motion_shows(bool p_visible_in_editor) const {
	return p_visible_in_editor || !Engine::get_singleton()->is_editor_hint();
}

void CameraAttributes::set_motion_blur_visible_in_editor(bool p_visible) {
	motion_blur_visible_in_editor = p_visible;
	_update_motion_blur();
	emit_changed();
}

bool CameraAttributes::is_motion_blur_visible_in_editor() const {
	return motion_blur_visible_in_editor;
}

void CameraAttributes::set_rolling_shutter_visible_in_editor(bool p_visible) {
	rolling_shutter_visible_in_editor = p_visible;
	_update_rolling_shutter();
	emit_changed();
}

bool CameraAttributes::is_rolling_shutter_visible_in_editor() const {
	return rolling_shutter_visible_in_editor;
}

void CameraAttributes::_update_motion_blur() {
	RS::get_singleton()->camera_attributes_set_motion_blur(camera_attributes, motion_blur_enabled && _camera_motion_shows(motion_blur_visible_in_editor), motion_blur_shutter_angle, _get_motion_blur_shutter_time(), motion_blur_strength, motion_blur_max_length);
}

void CameraAttributes::set_rolling_shutter_enabled(bool p_enabled) {
	rolling_shutter_enabled = p_enabled;
	_update_rolling_shutter();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_rolling_shutter_enabled() const {
	return rolling_shutter_enabled;
}

void CameraAttributes::set_rolling_shutter_readout_time(float p_time) {
	rolling_shutter_readout_time = p_time;
	_update_rolling_shutter();
	emit_changed();
}

float CameraAttributes::get_rolling_shutter_readout_time() const {
	return rolling_shutter_readout_time;
}

void CameraAttributes::set_frame_rate(float p_frame_rate) {
	frame_rate = MAX(p_frame_rate, 0.0f);
	_update_frame_rate();
	_update_exposure(); // CameraAttributesPhysical's Angle mode exposes for a share of this frame.
	notify_property_list_changed();
	emit_changed();
}

float CameraAttributes::get_frame_rate() const {
	return frame_rate;
}

void CameraAttributes::set_frame_rate_hold(bool p_hold) {
	frame_rate_hold = p_hold;
	_update_frame_rate();
	emit_changed();
}

bool CameraAttributes::is_frame_rate_hold() const {
	return frame_rate_hold;
}

void CameraAttributes::_update_frame_rate() {
	// Holding frames is a playback effect, so the editor's own viewports never hold (as motion blur is opt-in there).
	RS::get_singleton()->camera_attributes_set_frame_rate(camera_attributes, frame_rate, frame_rate_hold && !Engine::get_singleton()->is_editor_hint());
}

void CameraAttributes::_update_rolling_shutter() {
	RS::get_singleton()->camera_attributes_set_rolling_shutter(camera_attributes, rolling_shutter_enabled && _camera_motion_shows(rolling_shutter_visible_in_editor), rolling_shutter_readout_time);
}

// Lens optics: the effects themselves are described in RendererCameraAttributes (camera_attributes_storage.cpp) and
// camera_lens_inc.glsl.

void CameraAttributes::set_vignetting_enabled(bool p_enabled) {
	vignetting_enabled = p_enabled;
	_update_vignetting();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_vignetting_enabled() const {
	return vignetting_enabled;
}

void CameraAttributes::set_vignetting_strength(float p_strength) {
	vignetting_strength = MAX(p_strength, 0.0f);
	_update_vignetting();
	emit_changed();
}

float CameraAttributes::get_vignetting_strength() const {
	return vignetting_strength;
}

void CameraAttributes::set_vignetting_falloff(float p_falloff) {
	vignetting_falloff = MAX(p_falloff, 0.1f);
	_update_vignetting();
	emit_changed();
}

float CameraAttributes::get_vignetting_falloff() const {
	return vignetting_falloff;
}

void CameraAttributes::_update_vignetting() {
	RS::get_singleton()->camera_attributes_set_vignetting(camera_attributes, vignetting_enabled, vignetting_strength, vignetting_falloff);
}

void CameraAttributes::set_distortion_enabled(bool p_enabled) {
	distortion_enabled = p_enabled;
	_update_distortion();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_distortion_enabled() const {
	return distortion_enabled;
}

void CameraAttributes::set_distortion_automatic(bool p_automatic) {
	distortion_automatic = p_automatic;
	_update_distortion();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_distortion_automatic() const {
	return distortion_automatic;
}

void CameraAttributes::set_distortion_k1(float p_k1) {
	distortion_k1 = CLAMP(p_k1, -0.5f, 0.5f);
	_update_distortion();
	emit_changed();
}

float CameraAttributes::get_distortion_k1() const {
	return distortion_k1;
}

void CameraAttributes::set_distortion_k2(float p_k2) {
	distortion_k2 = CLAMP(p_k2, -0.5f, 0.5f);
	_update_distortion();
	emit_changed();
}

float CameraAttributes::get_distortion_k2() const {
	return distortion_k2;
}

void CameraAttributes::set_distortion_auto_crop(bool p_auto_crop) {
	distortion_auto_crop = p_auto_crop;
	_update_distortion();
	emit_changed();
}

bool CameraAttributes::is_distortion_auto_crop() const {
	return distortion_auto_crop;
}

void CameraAttributes::_update_distortion() {
	RS::get_singleton()->camera_attributes_set_lens_distortion(camera_attributes, distortion_enabled, distortion_automatic, distortion_k1, distortion_k2, distortion_auto_crop);
}

void CameraAttributes::set_chromatic_aberration_enabled(bool p_enabled) {
	chromatic_aberration_enabled = p_enabled;
	_update_chromatic_aberration();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_chromatic_aberration_enabled() const {
	return chromatic_aberration_enabled;
}

void CameraAttributes::set_chromatic_aberration_strength(float p_strength) {
	chromatic_aberration_strength = p_strength;
	_update_chromatic_aberration();
	emit_changed();
}

float CameraAttributes::get_chromatic_aberration_strength() const {
	return chromatic_aberration_strength;
}

void CameraAttributes::_update_chromatic_aberration() {
	RS::get_singleton()->camera_attributes_set_chromatic_aberration(camera_attributes, chromatic_aberration_enabled, chromatic_aberration_strength);
}

void CameraAttributes::set_aperture_enabled(bool p_value) {
	aperture_enabled = p_value;
	_update_aperture();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_aperture_enabled() const {
	return aperture_enabled;
}

void CameraAttributes::set_aperture_blade_count(int p_value) {
	aperture_blade_count = p_value < 3 ? 0 : MIN(p_value, 32);
	_update_aperture();
	emit_changed();
}

int CameraAttributes::get_aperture_blade_count() const {
	return aperture_blade_count;
}

void CameraAttributes::set_aperture_blade_curvature(float p_value) {
	aperture_blade_curvature = CLAMP(p_value, 0.0f, 1.0f);
	_update_aperture();
	emit_changed();
}

float CameraAttributes::get_aperture_blade_curvature() const {
	return aperture_blade_curvature;
}

void CameraAttributes::set_aperture_rotation(float p_value) {
	aperture_rotation = p_value;
	_update_aperture();
	emit_changed();
}

float CameraAttributes::get_aperture_rotation() const {
	return aperture_rotation;
}

void CameraAttributes::set_cats_eye_enabled(bool p_value) {
	cats_eye_enabled = p_value;
	_update_cats_eye();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_cats_eye_enabled() const {
	return cats_eye_enabled;
}

void CameraAttributes::set_cats_eye_strength(float p_value) {
	cats_eye_strength = CLAMP(p_value, 0.0f, 2.0f);
	_update_cats_eye();
	emit_changed();
}

float CameraAttributes::get_cats_eye_strength() const {
	return cats_eye_strength;
}

void CameraAttributes::set_starburst_enabled(bool p_value) {
	starburst_enabled = p_value;
	_update_starburst();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_starburst_enabled() const {
	return starburst_enabled;
}

void CameraAttributes::set_starburst_strength(float p_value) {
	starburst_strength = MAX(p_value, 0.0f);
	_update_starburst();
	emit_changed();
}

float CameraAttributes::get_starburst_strength() const {
	return starburst_strength;
}

void CameraAttributes::set_halation_enabled(bool p_value) {
	halation_enabled = p_value;
	_update_halation();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_halation_enabled() const {
	return halation_enabled;
}

void CameraAttributes::set_halation_strength(float p_value) {
	halation_strength = CLAMP(p_value, 0.0f, 12.0f);
	_update_halation();
	emit_changed();
}

float CameraAttributes::get_halation_strength() const {
	return halation_strength;
}

void CameraAttributes::set_halation_radius(float p_value) {
	halation_radius = MAX(p_value, 0.01f);
	_update_halation();
	emit_changed();
}

float CameraAttributes::get_halation_radius() const {
	return halation_radius;
}

void CameraAttributes::set_longitudinal_chromatic_aberration_enabled(bool p_value) {
	longitudinal_chromatic_aberration_enabled = p_value;
	_update_longitudinal_chromatic_aberration();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_longitudinal_chromatic_aberration_enabled() const {
	return longitudinal_chromatic_aberration_enabled;
}

void CameraAttributes::set_longitudinal_chromatic_aberration_strength(float p_value) {
	longitudinal_chromatic_aberration_strength = MAX(p_value, 0.0f);
	_update_longitudinal_chromatic_aberration();
	emit_changed();
}

float CameraAttributes::get_longitudinal_chromatic_aberration_strength() const {
	return longitudinal_chromatic_aberration_strength;
}

void CameraAttributes::_update_longitudinal_chromatic_aberration() {
	RS::get_singleton()->camera_attributes_set_longitudinal_chromatic_aberration(camera_attributes, longitudinal_chromatic_aberration_enabled, longitudinal_chromatic_aberration_strength);
}

void CameraAttributes::set_halation_high_quality(bool p_enabled) {
	halation_high_quality = p_enabled;
	RS::get_singleton()->camera_attributes_set_halation_high_quality(camera_attributes, halation_high_quality);
	emit_changed();
}

bool CameraAttributes::is_halation_high_quality() const {
	return halation_high_quality;
}

void CameraAttributes::set_panini_enabled(bool p_enabled) {
	panini_enabled = p_enabled;
	_update_panini();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributes::is_panini_enabled() const {
	return panini_enabled;
}

void CameraAttributes::set_panini_distance(float p_distance) {
	panini_distance = CLAMP(p_distance, 0.0f, 1.0f);
	_update_panini();
	emit_changed();
}

float CameraAttributes::get_panini_distance() const {
	return panini_distance;
}

void CameraAttributes::set_panini_vertical_compression(float p_compression) {
	panini_vertical_compression = CLAMP(p_compression, 0.0f, 1.0f);
	_update_panini();
	emit_changed();
}

float CameraAttributes::get_panini_vertical_compression() const {
	return panini_vertical_compression;
}

bool CameraAttributes::get_panini_projection(float &r_distance, float &r_vertical_compression) const {
	r_distance = panini_distance;
	r_vertical_compression = panini_vertical_compression;
	// The same condition as the renderer's (RendererCameraAttributes::_lens_requested_bits()).
	return panini_enabled && panini_distance > 0.0f;
}

void CameraAttributes::_update_panini() {
	RS::get_singleton()->camera_attributes_set_panini(camera_attributes, panini_enabled, panini_distance, panini_vertical_compression);
}

void CameraAttributes::_update_aperture() {
	RS::get_singleton()->camera_attributes_set_aperture(camera_attributes, aperture_enabled, aperture_blade_count, aperture_blade_curvature, aperture_rotation);
}

void CameraAttributes::_update_cats_eye() {
	RS::get_singleton()->camera_attributes_set_cats_eye(camera_attributes, cats_eye_enabled, cats_eye_strength);
}

void CameraAttributes::_update_starburst() {
	RS::get_singleton()->camera_attributes_set_starburst(camera_attributes, starburst_enabled, starburst_strength);
}

void CameraAttributes::_update_halation() {
	RS::get_singleton()->camera_attributes_set_halation(camera_attributes, halation_enabled, halation_strength, halation_radius);
}

RID CameraAttributes::get_rid() const {
	return camera_attributes;
}

void CameraAttributes::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// The sensitivity sets the grain's ISO too, so it shows whenever grain is on.
	if (!GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/use_physical_light_units") && !grain_enabled && p_property.name == "exposure_sensitivity") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("auto_exposure_") && p_property.name != "auto_exposure_enabled" && !auto_exposure_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (((p_property.name == "auto_exposure_metering_spot_size" || p_property.name == "auto_exposure_metering_spot_point") && auto_exposure_metering_mode != METERING_MODE_SPOT) ||
			(p_property.name == "auto_exposure_metering_highlight_percentile" && auto_exposure_metering_mode != METERING_MODE_HIGHLIGHT_WEIGHTED) ||
			(p_property.name.begins_with("white_balance_auto_") && !white_balance_auto)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("grain_") && p_property.name != "grain_enabled" && !grain_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("noise_reduction_") && p_property.name != "noise_reduction_enabled" && !noise_reduction_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("sharpening_") && p_property.name != "sharpening_enabled" && !sharpening_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("lut_") && p_property.name != "lut_enabled" && !lut_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("motion_blur_") && p_property.name != "motion_blur_enabled" && !motion_blur_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name == "frame_rate_hold" && frame_rate <= 0.0f) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (p_property.name.begins_with("rolling_shutter_") && p_property.name != "rolling_shutter_enabled" && !rolling_shutter_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if ((p_property.name.begins_with("vignetting_") && p_property.name != "vignetting_enabled" && !vignetting_enabled) ||
			(p_property.name.begins_with("distortion_") && p_property.name != "distortion_enabled" && !distortion_enabled) ||
			(p_property.name.begins_with("chromatic_aberration_") && p_property.name != "chromatic_aberration_enabled" && !chromatic_aberration_enabled)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	// The starburst follows the aperture's blades, so they show when either is on.
	if ((p_property.name.begins_with("aperture_") && p_property.name != "aperture_enabled" && !aperture_enabled && !starburst_enabled) ||
			(p_property.name.begins_with("cats_eye_") && p_property.name != "cats_eye_enabled" && !cats_eye_enabled) ||
			(p_property.name.begins_with("starburst_") && p_property.name != "starburst_enabled" && !starburst_enabled) ||
			(p_property.name.begins_with("halation_") && p_property.name != "halation_enabled" && !halation_enabled)) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("longitudinal_chromatic_aberration_") && p_property.name != "longitudinal_chromatic_aberration_enabled" && !longitudinal_chromatic_aberration_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("panini_") && p_property.name != "panini_enabled" && !panini_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}

	if (p_property.name.begins_with("local_exposure_") && p_property.name != "local_exposure_enabled" && !local_exposure_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
		return;
	}
}

void CameraAttributes::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_exposure_multiplier", "multiplier"), &CameraAttributes::set_exposure_multiplier);
	ClassDB::bind_method(D_METHOD("get_exposure_multiplier"), &CameraAttributes::get_exposure_multiplier);
	ClassDB::bind_method(D_METHOD("set_exposure_sensitivity", "sensitivity"), &CameraAttributes::set_exposure_sensitivity);
	ClassDB::bind_method(D_METHOD("get_exposure_sensitivity"), &CameraAttributes::get_exposure_sensitivity);

	ClassDB::bind_method(D_METHOD("set_auto_exposure_enabled", "enabled"), &CameraAttributes::set_auto_exposure_enabled);
	ClassDB::bind_method(D_METHOD("is_auto_exposure_enabled"), &CameraAttributes::is_auto_exposure_enabled);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_speed", "exposure_speed"), &CameraAttributes::set_auto_exposure_speed);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_speed"), &CameraAttributes::get_auto_exposure_speed);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_scale", "exposure_grey"), &CameraAttributes::set_auto_exposure_scale);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_scale"), &CameraAttributes::get_auto_exposure_scale);

	ClassDB::bind_method(D_METHOD("set_auto_exposure_metering_mode", "mode"), &CameraAttributes::set_auto_exposure_metering_mode);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_metering_mode"), &CameraAttributes::get_auto_exposure_metering_mode);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_metering_spot_size", "size"), &CameraAttributes::set_auto_exposure_metering_spot_size);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_metering_spot_size"), &CameraAttributes::get_auto_exposure_metering_spot_size);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_metering_spot_point", "point"), &CameraAttributes::set_auto_exposure_metering_spot_point);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_metering_spot_point"), &CameraAttributes::get_auto_exposure_metering_spot_point);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_metering_highlight_percentile", "percentile"), &CameraAttributes::set_auto_exposure_metering_highlight_percentile);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_metering_highlight_percentile"), &CameraAttributes::get_auto_exposure_metering_highlight_percentile);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_metering_log_average", "enabled"), &CameraAttributes::set_auto_exposure_metering_log_average);
	ClassDB::bind_method(D_METHOD("is_auto_exposure_metering_log_average"), &CameraAttributes::is_auto_exposure_metering_log_average);

	ClassDB::bind_method(D_METHOD("set_white_balance_auto", "enabled"), &CameraAttributes::set_white_balance_auto);
	ClassDB::bind_method(D_METHOD("is_white_balance_auto"), &CameraAttributes::is_white_balance_auto);
	ClassDB::bind_method(D_METHOD("set_white_balance_auto_speed", "speed"), &CameraAttributes::set_white_balance_auto_speed);
	ClassDB::bind_method(D_METHOD("get_white_balance_auto_speed"), &CameraAttributes::get_white_balance_auto_speed);
	ClassDB::bind_method(D_METHOD("set_white_balance_auto_min_temperature", "temperature"), &CameraAttributes::set_white_balance_auto_min_temperature);
	ClassDB::bind_method(D_METHOD("get_white_balance_auto_min_temperature"), &CameraAttributes::get_white_balance_auto_min_temperature);
	ClassDB::bind_method(D_METHOD("set_white_balance_auto_max_temperature", "temperature"), &CameraAttributes::set_white_balance_auto_max_temperature);
	ClassDB::bind_method(D_METHOD("get_white_balance_auto_max_temperature"), &CameraAttributes::get_white_balance_auto_max_temperature);

	ClassDB::bind_method(D_METHOD("set_white_balance_temperature", "temperature"), &CameraAttributes::set_white_balance_temperature);
	ClassDB::bind_method(D_METHOD("get_white_balance_temperature"), &CameraAttributes::get_white_balance_temperature);
	ClassDB::bind_method(D_METHOD("set_white_balance_tint", "tint"), &CameraAttributes::set_white_balance_tint);
	ClassDB::bind_method(D_METHOD("get_white_balance_tint"), &CameraAttributes::get_white_balance_tint);

	ClassDB::bind_method(D_METHOD("set_grain_enabled", "enabled"), &CameraAttributes::set_grain_enabled);
	ClassDB::bind_method(D_METHOD("is_grain_enabled"), &CameraAttributes::is_grain_enabled);
	ClassDB::bind_method(D_METHOD("set_grain_type", "type"), &CameraAttributes::set_grain_type);
	ClassDB::bind_method(D_METHOD("get_grain_type"), &CameraAttributes::get_grain_type);
	ClassDB::bind_method(D_METHOD("set_grain_intensity", "intensity"), &CameraAttributes::set_grain_intensity);
	ClassDB::bind_method(D_METHOD("get_grain_intensity"), &CameraAttributes::get_grain_intensity);
	ClassDB::bind_method(D_METHOD("set_grain_size", "size"), &CameraAttributes::set_grain_size);
	ClassDB::bind_method(D_METHOD("get_grain_size"), &CameraAttributes::get_grain_size);

	ClassDB::bind_method(D_METHOD("set_noise_reduction_enabled", "enabled"), &CameraAttributes::set_noise_reduction_enabled);
	ClassDB::bind_method(D_METHOD("is_noise_reduction_enabled"), &CameraAttributes::is_noise_reduction_enabled);
	ClassDB::bind_method(D_METHOD("set_noise_reduction_strength", "strength"), &CameraAttributes::set_noise_reduction_strength);
	ClassDB::bind_method(D_METHOD("get_noise_reduction_strength"), &CameraAttributes::get_noise_reduction_strength);
	ClassDB::bind_method(D_METHOD("set_noise_reduction_chroma_only", "chroma_only"), &CameraAttributes::set_noise_reduction_chroma_only);
	ClassDB::bind_method(D_METHOD("is_noise_reduction_chroma_only"), &CameraAttributes::is_noise_reduction_chroma_only);

	ClassDB::bind_method(D_METHOD("set_sharpening_enabled", "enabled"), &CameraAttributes::set_sharpening_enabled);
	ClassDB::bind_method(D_METHOD("is_sharpening_enabled"), &CameraAttributes::is_sharpening_enabled);
	ClassDB::bind_method(D_METHOD("set_sharpening_amount", "amount"), &CameraAttributes::set_sharpening_amount);
	ClassDB::bind_method(D_METHOD("get_sharpening_amount"), &CameraAttributes::get_sharpening_amount);
	ClassDB::bind_method(D_METHOD("set_sharpening_radius", "radius"), &CameraAttributes::set_sharpening_radius);
	ClassDB::bind_method(D_METHOD("get_sharpening_radius"), &CameraAttributes::get_sharpening_radius);
	ClassDB::bind_method(D_METHOD("set_sharpening_halo", "halo"), &CameraAttributes::set_sharpening_halo);
	ClassDB::bind_method(D_METHOD("get_sharpening_halo"), &CameraAttributes::get_sharpening_halo);

	ClassDB::bind_method(D_METHOD("set_lut_enabled", "enabled"), &CameraAttributes::set_lut_enabled);
	ClassDB::bind_method(D_METHOD("is_lut_enabled"), &CameraAttributes::is_lut_enabled);
	ClassDB::bind_method(D_METHOD("set_lut_texture", "texture"), &CameraAttributes::set_lut_texture);
	ClassDB::bind_method(D_METHOD("get_lut_texture"), &CameraAttributes::get_lut_texture);
	ClassDB::bind_method(D_METHOD("set_lut_input", "input"), &CameraAttributes::set_lut_input);
	ClassDB::bind_method(D_METHOD("get_lut_input"), &CameraAttributes::get_lut_input);
	ClassDB::bind_method(D_METHOD("set_lut_strength", "strength"), &CameraAttributes::set_lut_strength);
	ClassDB::bind_method(D_METHOD("get_lut_strength"), &CameraAttributes::get_lut_strength);

	ClassDB::bind_method(D_METHOD("set_motion_blur_enabled", "enabled"), &CameraAttributes::set_motion_blur_enabled);
	ClassDB::bind_method(D_METHOD("is_motion_blur_enabled"), &CameraAttributes::is_motion_blur_enabled);
	ClassDB::bind_method(D_METHOD("set_shutter_angle", "angle"), &CameraAttributes::set_shutter_angle);
	ClassDB::bind_method(D_METHOD("get_shutter_angle"), &CameraAttributes::get_shutter_angle);
	ClassDB::bind_method(D_METHOD("set_motion_blur_shutter_angle", "angle"), &CameraAttributes::set_motion_blur_shutter_angle);
	ClassDB::bind_method(D_METHOD("get_motion_blur_shutter_angle"), &CameraAttributes::get_motion_blur_shutter_angle);
	ClassDB::bind_method(D_METHOD("set_motion_blur_strength", "strength"), &CameraAttributes::set_motion_blur_strength);
	ClassDB::bind_method(D_METHOD("get_motion_blur_strength"), &CameraAttributes::get_motion_blur_strength);
	ClassDB::bind_method(D_METHOD("set_motion_blur_max_length", "length"), &CameraAttributes::set_motion_blur_max_length);
	ClassDB::bind_method(D_METHOD("get_motion_blur_max_length"), &CameraAttributes::get_motion_blur_max_length);

	ClassDB::bind_method(D_METHOD("set_rolling_shutter_enabled", "enabled"), &CameraAttributes::set_rolling_shutter_enabled);
	ClassDB::bind_method(D_METHOD("is_rolling_shutter_enabled"), &CameraAttributes::is_rolling_shutter_enabled);
	ClassDB::bind_method(D_METHOD("set_rolling_shutter_readout_time", "time"), &CameraAttributes::set_rolling_shutter_readout_time);
	ClassDB::bind_method(D_METHOD("get_rolling_shutter_readout_time"), &CameraAttributes::get_rolling_shutter_readout_time);
	ClassDB::bind_method(D_METHOD("set_motion_blur_visible_in_editor", "visible"), &CameraAttributes::set_motion_blur_visible_in_editor);
	ClassDB::bind_method(D_METHOD("is_motion_blur_visible_in_editor"), &CameraAttributes::is_motion_blur_visible_in_editor);
	ClassDB::bind_method(D_METHOD("set_frame_rate", "frame_rate"), &CameraAttributes::set_frame_rate);
	ClassDB::bind_method(D_METHOD("get_frame_rate"), &CameraAttributes::get_frame_rate);
	ClassDB::bind_method(D_METHOD("set_frame_rate_hold", "hold"), &CameraAttributes::set_frame_rate_hold);
	ClassDB::bind_method(D_METHOD("is_frame_rate_hold"), &CameraAttributes::is_frame_rate_hold);
	ClassDB::bind_method(D_METHOD("set_rolling_shutter_visible_in_editor", "visible"), &CameraAttributes::set_rolling_shutter_visible_in_editor);
	ClassDB::bind_method(D_METHOD("is_rolling_shutter_visible_in_editor"), &CameraAttributes::is_rolling_shutter_visible_in_editor);

	ADD_GROUP("Exposure", "exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_sensitivity", PROPERTY_HINT_RANGE, "0.1,32000.0,0.1,suffix:ISO"), "set_exposure_sensitivity", "get_exposure_sensitivity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_multiplier", PROPERTY_HINT_RANGE, "0.0,8.0,0.001,or_greater"), "set_exposure_multiplier", "get_exposure_multiplier");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_shutter_angle", PROPERTY_HINT_RANGE, "1,360,0.1,degrees"), "set_shutter_angle", "get_shutter_angle");

	ADD_GROUP("Auto Exposure", "auto_exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_exposure_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_auto_exposure_enabled", "is_auto_exposure_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_scale", PROPERTY_HINT_RANGE, "0.01,64,0.01"), "set_auto_exposure_scale", "get_auto_exposure_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_speed", PROPERTY_HINT_RANGE, "0.01,64,0.01"), "set_auto_exposure_speed", "get_auto_exposure_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "auto_exposure_metering_mode", PROPERTY_HINT_ENUM, "Average,Center-Weighted,Spot,Highlight-Weighted"), "set_auto_exposure_metering_mode", "get_auto_exposure_metering_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_metering_spot_size", PROPERTY_HINT_RANGE, "0.1,20,0.1,or_greater,suffix:%"), "set_auto_exposure_metering_spot_size", "get_auto_exposure_metering_spot_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "auto_exposure_metering_spot_point", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_auto_exposure_metering_spot_point", "get_auto_exposure_metering_spot_point");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_metering_highlight_percentile", PROPERTY_HINT_RANGE, "50,100,0.1,suffix:%"), "set_auto_exposure_metering_highlight_percentile", "get_auto_exposure_metering_highlight_percentile");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_exposure_metering_log_average"), "set_auto_exposure_metering_log_average", "is_auto_exposure_metering_log_average");

	ADD_GROUP("White Balance", "white_balance_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "white_balance_temperature", PROPERTY_HINT_RANGE, "2000,10000,1,suffix:K"), "set_white_balance_temperature", "get_white_balance_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "white_balance_tint", PROPERTY_HINT_RANGE, "-150,150,1"), "set_white_balance_tint", "get_white_balance_tint");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "white_balance_auto"), "set_white_balance_auto", "is_white_balance_auto");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "white_balance_auto_speed", PROPERTY_HINT_RANGE, "0.01,16,0.01,or_greater"), "set_white_balance_auto_speed", "get_white_balance_auto_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "white_balance_auto_min_temperature", PROPERTY_HINT_RANGE, "1000,15000,1,suffix:K"), "set_white_balance_auto_min_temperature", "get_white_balance_auto_min_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "white_balance_auto_max_temperature", PROPERTY_HINT_RANGE, "1000,15000,1,suffix:K"), "set_white_balance_auto_max_temperature", "get_white_balance_auto_max_temperature");

	ClassDB::bind_method(D_METHOD("set_local_exposure_enabled", "enabled"), &CameraAttributes::set_local_exposure_enabled);
	ClassDB::bind_method(D_METHOD("is_local_exposure_enabled"), &CameraAttributes::is_local_exposure_enabled);
	ClassDB::bind_method(D_METHOD("set_local_exposure_highlight_contrast", "contrast"), &CameraAttributes::set_local_exposure_highlight_contrast);
	ClassDB::bind_method(D_METHOD("get_local_exposure_highlight_contrast"), &CameraAttributes::get_local_exposure_highlight_contrast);
	ClassDB::bind_method(D_METHOD("set_local_exposure_shadow_contrast", "contrast"), &CameraAttributes::set_local_exposure_shadow_contrast);
	ClassDB::bind_method(D_METHOD("get_local_exposure_shadow_contrast"), &CameraAttributes::get_local_exposure_shadow_contrast);
	ClassDB::bind_method(D_METHOD("set_local_exposure_detail_strength", "strength"), &CameraAttributes::set_local_exposure_detail_strength);
	ClassDB::bind_method(D_METHOD("get_local_exposure_detail_strength"), &CameraAttributes::get_local_exposure_detail_strength);
	ClassDB::bind_method(D_METHOD("set_local_exposure_middle_gray_bias", "bias"), &CameraAttributes::set_local_exposure_middle_gray_bias);
	ClassDB::bind_method(D_METHOD("get_local_exposure_middle_gray_bias"), &CameraAttributes::get_local_exposure_middle_gray_bias);
	ClassDB::bind_method(D_METHOD("set_local_exposure_blurred_luminance_blend", "blend"), &CameraAttributes::set_local_exposure_blurred_luminance_blend);
	ClassDB::bind_method(D_METHOD("get_local_exposure_blurred_luminance_blend"), &CameraAttributes::get_local_exposure_blurred_luminance_blend);
	ClassDB::bind_method(D_METHOD("set_local_exposure_blurred_luminance_kernel_size", "size"), &CameraAttributes::set_local_exposure_blurred_luminance_kernel_size);
	ClassDB::bind_method(D_METHOD("get_local_exposure_blurred_luminance_kernel_size"), &CameraAttributes::get_local_exposure_blurred_luminance_kernel_size);

	ADD_GROUP("Local Exposure", "local_exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local_exposure_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_local_exposure_enabled", "is_local_exposure_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_highlight_contrast", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_local_exposure_highlight_contrast", "get_local_exposure_highlight_contrast");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_shadow_contrast", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_local_exposure_shadow_contrast", "get_local_exposure_shadow_contrast");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_detail_strength", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_local_exposure_detail_strength", "get_local_exposure_detail_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_middle_gray_bias", PROPERTY_HINT_RANGE, "-5,5,0.01,or_less,or_greater,suffix:EV"), "set_local_exposure_middle_gray_bias", "get_local_exposure_middle_gray_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_blurred_luminance_blend", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_local_exposure_blurred_luminance_blend", "get_local_exposure_blurred_luminance_blend");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "local_exposure_blurred_luminance_kernel_size", PROPERTY_HINT_RANGE, "1,100,0.1,suffix:%"), "set_local_exposure_blurred_luminance_kernel_size", "get_local_exposure_blurred_luminance_kernel_size");

	ADD_GROUP("Grain", "grain_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "grain_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_grain_enabled", "is_grain_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grain_type", PROPERTY_HINT_ENUM, "Film,Film Monochrome,Digital"), "set_grain_type", "get_grain_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grain_intensity", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_grain_intensity", "get_grain_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grain_size", PROPERTY_HINT_RANGE, "0.25,4,0.01,or_greater"), "set_grain_size", "get_grain_size");

	ADD_GROUP("Noise Reduction", "noise_reduction_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "noise_reduction_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_noise_reduction_enabled", "is_noise_reduction_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_reduction_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_noise_reduction_strength", "get_noise_reduction_strength");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "noise_reduction_chroma_only"), "set_noise_reduction_chroma_only", "is_noise_reduction_chroma_only");

	ADD_GROUP("Sharpening", "sharpening_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sharpening_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_sharpening_enabled", "is_sharpening_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sharpening_amount", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_sharpening_amount", "get_sharpening_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sharpening_radius", PROPERTY_HINT_RANGE, "0.25,8,0.01,or_greater,suffix:px at 1080p"), "set_sharpening_radius", "get_sharpening_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sharpening_halo", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_sharpening_halo", "get_sharpening_halo");

	ClassDB::bind_method(D_METHOD("set_vignetting_enabled", "enabled"), &CameraAttributes::set_vignetting_enabled);
	ClassDB::bind_method(D_METHOD("is_vignetting_enabled"), &CameraAttributes::is_vignetting_enabled);
	ClassDB::bind_method(D_METHOD("set_vignetting_strength", "strength"), &CameraAttributes::set_vignetting_strength);
	ClassDB::bind_method(D_METHOD("get_vignetting_strength"), &CameraAttributes::get_vignetting_strength);
	ClassDB::bind_method(D_METHOD("set_vignetting_falloff", "falloff"), &CameraAttributes::set_vignetting_falloff);
	ClassDB::bind_method(D_METHOD("get_vignetting_falloff"), &CameraAttributes::get_vignetting_falloff);

	ClassDB::bind_method(D_METHOD("set_distortion_enabled", "enabled"), &CameraAttributes::set_distortion_enabled);
	ClassDB::bind_method(D_METHOD("is_distortion_enabled"), &CameraAttributes::is_distortion_enabled);
	ClassDB::bind_method(D_METHOD("set_distortion_automatic", "automatic"), &CameraAttributes::set_distortion_automatic);
	ClassDB::bind_method(D_METHOD("is_distortion_automatic"), &CameraAttributes::is_distortion_automatic);
	ClassDB::bind_method(D_METHOD("set_distortion_k1", "k1"), &CameraAttributes::set_distortion_k1);
	ClassDB::bind_method(D_METHOD("get_distortion_k1"), &CameraAttributes::get_distortion_k1);
	ClassDB::bind_method(D_METHOD("set_distortion_k2", "k2"), &CameraAttributes::set_distortion_k2);
	ClassDB::bind_method(D_METHOD("get_distortion_k2"), &CameraAttributes::get_distortion_k2);
	ClassDB::bind_method(D_METHOD("set_distortion_auto_crop", "auto_crop"), &CameraAttributes::set_distortion_auto_crop);
	ClassDB::bind_method(D_METHOD("is_distortion_auto_crop"), &CameraAttributes::is_distortion_auto_crop);

	ClassDB::bind_method(D_METHOD("set_chromatic_aberration_enabled", "enabled"), &CameraAttributes::set_chromatic_aberration_enabled);
	ClassDB::bind_method(D_METHOD("is_chromatic_aberration_enabled"), &CameraAttributes::is_chromatic_aberration_enabled);
	ClassDB::bind_method(D_METHOD("set_chromatic_aberration_strength", "strength"), &CameraAttributes::set_chromatic_aberration_strength);
	ClassDB::bind_method(D_METHOD("get_chromatic_aberration_strength"), &CameraAttributes::get_chromatic_aberration_strength);

	ADD_GROUP("LUT", "lut_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lut_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_lut_enabled", "is_lut_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "lut_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture3D"), "set_lut_texture", "get_lut_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lut_input", PROPERTY_HINT_ENUM, "Display (sRGB),ARRI LogC3 (AWG3),Sony S-Log3 (S-Gamut3.Cine),Panasonic V-Log (V-Gamut),Canon Log 3 (Cinema Gamut),RED Log3G10 (REDWideGamutRGB),ACEScct (AP1)"), "set_lut_input", "get_lut_input");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lut_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_lut_strength", "get_lut_strength");

	ADD_GROUP("Vignetting", "vignetting_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vignetting_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_vignetting_enabled", "is_vignetting_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vignetting_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_vignetting_strength", "get_vignetting_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vignetting_falloff", PROPERTY_HINT_RANGE, "0.1,8,0.01"), "set_vignetting_falloff", "get_vignetting_falloff");

	ADD_GROUP("Distortion", "distortion_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distortion_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_distortion_enabled", "is_distortion_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distortion_automatic"), "set_distortion_automatic", "is_distortion_automatic");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distortion_k1", PROPERTY_HINT_RANGE, "-0.5,0.5,0.001"), "set_distortion_k1", "get_distortion_k1");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "distortion_k2", PROPERTY_HINT_RANGE, "-0.5,0.5,0.001"), "set_distortion_k2", "get_distortion_k2");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distortion_auto_crop"), "set_distortion_auto_crop", "is_distortion_auto_crop");

	ADD_GROUP("Chromatic Aberration", "chromatic_aberration_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "chromatic_aberration_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_chromatic_aberration_enabled", "is_chromatic_aberration_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chromatic_aberration_strength", PROPERTY_HINT_RANGE, "-10,10,0.01,or_greater,or_less"), "set_chromatic_aberration_strength", "get_chromatic_aberration_strength");

	ClassDB::bind_method(D_METHOD("set_aperture_enabled", "enabled"), &CameraAttributes::set_aperture_enabled);
	ClassDB::bind_method(D_METHOD("is_aperture_enabled"), &CameraAttributes::is_aperture_enabled);
	ClassDB::bind_method(D_METHOD("set_aperture_blade_count", "count"), &CameraAttributes::set_aperture_blade_count);
	ClassDB::bind_method(D_METHOD("get_aperture_blade_count"), &CameraAttributes::get_aperture_blade_count);
	ClassDB::bind_method(D_METHOD("set_aperture_blade_curvature", "curvature"), &CameraAttributes::set_aperture_blade_curvature);
	ClassDB::bind_method(D_METHOD("get_aperture_blade_curvature"), &CameraAttributes::get_aperture_blade_curvature);
	ClassDB::bind_method(D_METHOD("set_aperture_rotation", "degrees"), &CameraAttributes::set_aperture_rotation);
	ClassDB::bind_method(D_METHOD("get_aperture_rotation"), &CameraAttributes::get_aperture_rotation);
	ClassDB::bind_method(D_METHOD("set_cats_eye_enabled", "enabled"), &CameraAttributes::set_cats_eye_enabled);
	ClassDB::bind_method(D_METHOD("is_cats_eye_enabled"), &CameraAttributes::is_cats_eye_enabled);
	ClassDB::bind_method(D_METHOD("set_cats_eye_strength", "strength"), &CameraAttributes::set_cats_eye_strength);
	ClassDB::bind_method(D_METHOD("get_cats_eye_strength"), &CameraAttributes::get_cats_eye_strength);
	ClassDB::bind_method(D_METHOD("set_starburst_enabled", "enabled"), &CameraAttributes::set_starburst_enabled);
	ClassDB::bind_method(D_METHOD("is_starburst_enabled"), &CameraAttributes::is_starburst_enabled);
	ClassDB::bind_method(D_METHOD("set_starburst_strength", "strength"), &CameraAttributes::set_starburst_strength);
	ClassDB::bind_method(D_METHOD("get_starburst_strength"), &CameraAttributes::get_starburst_strength);
	ClassDB::bind_method(D_METHOD("set_halation_enabled", "enabled"), &CameraAttributes::set_halation_enabled);
	ClassDB::bind_method(D_METHOD("is_halation_enabled"), &CameraAttributes::is_halation_enabled);
	ClassDB::bind_method(D_METHOD("set_halation_strength", "strength"), &CameraAttributes::set_halation_strength);
	ClassDB::bind_method(D_METHOD("get_halation_strength"), &CameraAttributes::get_halation_strength);
	ClassDB::bind_method(D_METHOD("set_halation_radius", "radius"), &CameraAttributes::set_halation_radius);
	ClassDB::bind_method(D_METHOD("get_halation_radius"), &CameraAttributes::get_halation_radius);
	ClassDB::bind_method(D_METHOD("set_halation_high_quality", "enabled"), &CameraAttributes::set_halation_high_quality);
	ClassDB::bind_method(D_METHOD("is_halation_high_quality"), &CameraAttributes::is_halation_high_quality);

	ADD_GROUP("Aperture", "aperture_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "aperture_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_aperture_enabled", "is_aperture_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "aperture_blade_count", PROPERTY_HINT_RANGE, "0,16,1,or_greater"), "set_aperture_blade_count", "get_aperture_blade_count");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "aperture_blade_curvature", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_aperture_blade_curvature", "get_aperture_blade_curvature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "aperture_rotation", PROPERTY_HINT_RANGE, "-180,180,0.1,degrees"), "set_aperture_rotation", "get_aperture_rotation");

	ADD_GROUP("Cat's Eye", "cats_eye_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cats_eye_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_cats_eye_enabled", "is_cats_eye_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cats_eye_strength", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_cats_eye_strength", "get_cats_eye_strength");

	ADD_GROUP("Starburst", "starburst_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "starburst_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_starburst_enabled", "is_starburst_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "starburst_strength", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,exp"), "set_starburst_strength", "get_starburst_strength");

	ADD_GROUP("Halation", "halation_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "halation_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_halation_enabled", "is_halation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "halation_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_halation_strength", "get_halation_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "halation_radius", PROPERTY_HINT_RANGE, "0.1,8,0.01,or_greater"), "set_halation_radius", "get_halation_radius");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "halation_high_quality"), "set_halation_high_quality", "is_halation_high_quality");

	ClassDB::bind_method(D_METHOD("set_longitudinal_chromatic_aberration_enabled", "enabled"), &CameraAttributes::set_longitudinal_chromatic_aberration_enabled);
	ClassDB::bind_method(D_METHOD("is_longitudinal_chromatic_aberration_enabled"), &CameraAttributes::is_longitudinal_chromatic_aberration_enabled);
	ClassDB::bind_method(D_METHOD("set_longitudinal_chromatic_aberration_strength", "strength"), &CameraAttributes::set_longitudinal_chromatic_aberration_strength);
	ClassDB::bind_method(D_METHOD("get_longitudinal_chromatic_aberration_strength"), &CameraAttributes::get_longitudinal_chromatic_aberration_strength);

	ADD_GROUP("Longitudinal Chromatic Aberration", "longitudinal_chromatic_aberration_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "longitudinal_chromatic_aberration_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_longitudinal_chromatic_aberration_enabled", "is_longitudinal_chromatic_aberration_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "longitudinal_chromatic_aberration_strength", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_longitudinal_chromatic_aberration_strength", "get_longitudinal_chromatic_aberration_strength");

	ClassDB::bind_method(D_METHOD("set_panini_enabled", "enabled"), &CameraAttributes::set_panini_enabled);
	ClassDB::bind_method(D_METHOD("is_panini_enabled"), &CameraAttributes::is_panini_enabled);
	ClassDB::bind_method(D_METHOD("set_panini_distance", "distance"), &CameraAttributes::set_panini_distance);
	ClassDB::bind_method(D_METHOD("get_panini_distance"), &CameraAttributes::get_panini_distance);
	ClassDB::bind_method(D_METHOD("set_panini_vertical_compression", "compression"), &CameraAttributes::set_panini_vertical_compression);
	ClassDB::bind_method(D_METHOD("get_panini_vertical_compression"), &CameraAttributes::get_panini_vertical_compression);

	ADD_GROUP("Panini Projection", "panini_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "panini_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_panini_enabled", "is_panini_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "panini_distance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_panini_distance", "get_panini_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "panini_vertical_compression", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_panini_vertical_compression", "get_panini_vertical_compression");

	ADD_GROUP("Motion Blur", "motion_blur_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "motion_blur_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_motion_blur_enabled", "is_motion_blur_enabled");
	// Moved to exposure_shutter_angle; still read from scenes saved before (not shown, not saved).
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "motion_blur_shutter_angle", PROPERTY_HINT_RANGE, "1,360,0.1,degrees", PROPERTY_USAGE_NONE), "set_motion_blur_shutter_angle", "get_motion_blur_shutter_angle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "motion_blur_strength", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_motion_blur_strength", "get_motion_blur_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "motion_blur_max_length", PROPERTY_HINT_RANGE, "0.001,0.25,0.001,or_greater"), "set_motion_blur_max_length", "get_motion_blur_max_length");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "motion_blur_visible_in_editor"), "set_motion_blur_visible_in_editor", "is_motion_blur_visible_in_editor");

	ADD_GROUP("Rolling Shutter", "rolling_shutter_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rolling_shutter_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_rolling_shutter_enabled", "is_rolling_shutter_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rolling_shutter_readout_time", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater,suffix:ms"), "set_rolling_shutter_readout_time", "get_rolling_shutter_readout_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "rolling_shutter_visible_in_editor"), "set_rolling_shutter_visible_in_editor", "is_rolling_shutter_visible_in_editor");

	ADD_GROUP("Frame Rate", "frame_rate_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frame_rate", PROPERTY_HINT_RANGE, "0,240,0.001,or_greater,suffix:fps"), "set_frame_rate", "get_frame_rate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "frame_rate_hold"), "set_frame_rate_hold", "is_frame_rate_hold");
	ADD_GROUP("", "");

	BIND_ENUM_CONSTANT(GRAIN_TYPE_FILM);
	BIND_ENUM_CONSTANT(GRAIN_TYPE_FILM_MONOCHROME);
	BIND_ENUM_CONSTANT(GRAIN_TYPE_DIGITAL);

	BIND_ENUM_CONSTANT(LUT_INPUT_DISPLAY);
	BIND_ENUM_CONSTANT(LUT_INPUT_ARRI_LOGC3);
	BIND_ENUM_CONSTANT(LUT_INPUT_SONY_SLOG3);
	BIND_ENUM_CONSTANT(LUT_INPUT_PANASONIC_VLOG);
	BIND_ENUM_CONSTANT(LUT_INPUT_CANON_LOG3);
	BIND_ENUM_CONSTANT(LUT_INPUT_RED_LOG3G10);
	BIND_ENUM_CONSTANT(LUT_INPUT_ACESCCT);
	BIND_ENUM_CONSTANT(LUT_INPUT_MAX);

	BIND_ENUM_CONSTANT(METERING_MODE_AVERAGE);
	BIND_ENUM_CONSTANT(METERING_MODE_CENTER_WEIGHTED);
	BIND_ENUM_CONSTANT(METERING_MODE_SPOT);
	BIND_ENUM_CONSTANT(METERING_MODE_HIGHLIGHT_WEIGHTED);
}

CameraAttributes::CameraAttributes() {
	camera_attributes = RS::get_singleton()->camera_attributes_create();
}

CameraAttributes::~CameraAttributes() {
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	RS::get_singleton()->free_rid(camera_attributes);
}

//////////////////////////////////////////////////////
/* CameraAttributesPractical */

void CameraAttributesPractical::set_dof_blur_far_enabled(bool p_enabled) {
	dof_blur_far_enabled = p_enabled;
	_update_dof_blur();
	notify_property_list_changed();
}

bool CameraAttributesPractical::is_dof_blur_far_enabled() const {
	return dof_blur_far_enabled;
}

void CameraAttributesPractical::set_dof_blur_far_distance(float p_distance) {
	dof_blur_far_distance = p_distance;
	_update_dof_blur();
}

float CameraAttributesPractical::get_dof_blur_far_distance() const {
	return dof_blur_far_distance;
}

void CameraAttributesPractical::set_dof_blur_far_transition(float p_distance) {
	dof_blur_far_transition = p_distance;
	_update_dof_blur();
}

float CameraAttributesPractical::get_dof_blur_far_transition() const {
	return dof_blur_far_transition;
}

void CameraAttributesPractical::set_dof_blur_near_enabled(bool p_enabled) {
	dof_blur_near_enabled = p_enabled;
	_update_dof_blur();
	notify_property_list_changed();
}

bool CameraAttributesPractical::is_dof_blur_near_enabled() const {
	return dof_blur_near_enabled;
}

void CameraAttributesPractical::set_dof_blur_near_distance(float p_distance) {
	dof_blur_near_distance = p_distance;
	_update_dof_blur();
}

float CameraAttributesPractical::get_dof_blur_near_distance() const {
	return dof_blur_near_distance;
}

void CameraAttributesPractical::set_dof_blur_near_transition(float p_distance) {
	dof_blur_near_transition = p_distance;
	_update_dof_blur();
}

float CameraAttributesPractical::get_dof_blur_near_transition() const {
	return dof_blur_near_transition;
}

void CameraAttributesPractical::set_dof_blur_amount(float p_amount) {
	dof_blur_amount = p_amount;
	_update_dof_blur();
}

float CameraAttributesPractical::get_dof_blur_amount() const {
	return dof_blur_amount;
}

void CameraAttributesPractical::_update_dof_blur() {
	RS::get_singleton()->camera_attributes_set_dof_blur(
			get_rid(),
			dof_blur_far_enabled,
			dof_blur_far_distance,
			dof_blur_far_transition,
			dof_blur_near_enabled,
			dof_blur_near_distance,
			dof_blur_near_transition,
			dof_blur_amount);
}

float CameraAttributesPractical::calculate_exposure_normalization() const {
	return exposure_sensitivity / 3072007.0; // Matches exposure normalization for default CameraAttributesPhysical at ISO 100.
}

void CameraAttributesPractical::set_auto_exposure_min_sensitivity(float p_min) {
	auto_exposure_min = p_min;
	_update_auto_exposure();
}

float CameraAttributesPractical::get_auto_exposure_min_sensitivity() const {
	return auto_exposure_min;
}

void CameraAttributesPractical::set_auto_exposure_max_sensitivity(float p_max) {
	auto_exposure_max = p_max;
	_update_auto_exposure();
}

float CameraAttributesPractical::get_auto_exposure_max_sensitivity() const {
	return auto_exposure_max;
}

void CameraAttributesPractical::_update_auto_exposure() {
	RS::get_singleton()->camera_attributes_set_auto_exposure(
			get_rid(),
			auto_exposure_enabled,
			auto_exposure_min * ((12.5 / 100.0) / exposure_sensitivity), // Convert from Sensitivity to Luminance
			auto_exposure_max * ((12.5 / 100.0) / exposure_sensitivity), // Convert from Sensitivity to Luminance
			auto_exposure_speed,
			auto_exposure_scale);
	emit_changed();
}

void CameraAttributesPractical::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if ((p_property.name != "dof_blur_far_enabled" && !dof_blur_far_enabled && p_property.name.begins_with("dof_blur_far_")) ||
			(p_property.name != "dof_blur_near_enabled" && !dof_blur_near_enabled && p_property.name.begins_with("dof_blur_near_"))) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	// Automatic distortion needs a focal length.
	if (p_property.name == "distortion_automatic") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void CameraAttributesPractical::_bind_methods() {
	// DOF blur

	ClassDB::bind_method(D_METHOD("set_dof_blur_far_enabled", "enabled"), &CameraAttributesPractical::set_dof_blur_far_enabled);
	ClassDB::bind_method(D_METHOD("is_dof_blur_far_enabled"), &CameraAttributesPractical::is_dof_blur_far_enabled);
	ClassDB::bind_method(D_METHOD("set_dof_blur_far_distance", "distance"), &CameraAttributesPractical::set_dof_blur_far_distance);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_distance"), &CameraAttributesPractical::get_dof_blur_far_distance);
	ClassDB::bind_method(D_METHOD("set_dof_blur_far_transition", "distance"), &CameraAttributesPractical::set_dof_blur_far_transition);
	ClassDB::bind_method(D_METHOD("get_dof_blur_far_transition"), &CameraAttributesPractical::get_dof_blur_far_transition);

	ClassDB::bind_method(D_METHOD("set_dof_blur_near_enabled", "enabled"), &CameraAttributesPractical::set_dof_blur_near_enabled);
	ClassDB::bind_method(D_METHOD("is_dof_blur_near_enabled"), &CameraAttributesPractical::is_dof_blur_near_enabled);
	ClassDB::bind_method(D_METHOD("set_dof_blur_near_distance", "distance"), &CameraAttributesPractical::set_dof_blur_near_distance);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_distance"), &CameraAttributesPractical::get_dof_blur_near_distance);
	ClassDB::bind_method(D_METHOD("set_dof_blur_near_transition", "distance"), &CameraAttributesPractical::set_dof_blur_near_transition);
	ClassDB::bind_method(D_METHOD("get_dof_blur_near_transition"), &CameraAttributesPractical::get_dof_blur_near_transition);
	ClassDB::bind_method(D_METHOD("set_dof_blur_amount", "amount"), &CameraAttributesPractical::set_dof_blur_amount);
	ClassDB::bind_method(D_METHOD("get_dof_blur_amount"), &CameraAttributesPractical::get_dof_blur_amount);

	ClassDB::bind_method(D_METHOD("set_auto_exposure_max_sensitivity", "max_sensitivity"), &CameraAttributesPractical::set_auto_exposure_max_sensitivity);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_max_sensitivity"), &CameraAttributesPractical::get_auto_exposure_max_sensitivity);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_min_sensitivity", "min_sensitivity"), &CameraAttributesPractical::set_auto_exposure_min_sensitivity);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_min_sensitivity"), &CameraAttributesPractical::get_auto_exposure_min_sensitivity);

	ADD_GROUP("DOF Blur", "dof_blur_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_blur_far_enabled"), "set_dof_blur_far_enabled", "is_dof_blur_far_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dof_blur_far_distance", PROPERTY_HINT_RANGE, "0.01,8192,0.01,exp,suffix:m"), "set_dof_blur_far_distance", "get_dof_blur_far_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dof_blur_far_transition", PROPERTY_HINT_RANGE, "-1,8192,0.01"), "set_dof_blur_far_transition", "get_dof_blur_far_transition");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_blur_near_enabled"), "set_dof_blur_near_enabled", "is_dof_blur_near_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dof_blur_near_distance", PROPERTY_HINT_RANGE, "0.01,8192,0.01,exp,suffix:m"), "set_dof_blur_near_distance", "get_dof_blur_near_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dof_blur_near_transition", PROPERTY_HINT_RANGE, "-1,8192,0.01"), "set_dof_blur_near_transition", "get_dof_blur_near_transition");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dof_blur_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_dof_blur_amount", "get_dof_blur_amount");

	ADD_GROUP("Auto Exposure", "auto_exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_min_sensitivity", PROPERTY_HINT_RANGE, "0,1600,0.01,or_greater,suffic:ISO"), "set_auto_exposure_min_sensitivity", "get_auto_exposure_min_sensitivity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_max_sensitivity", PROPERTY_HINT_RANGE, "0,64000,0.1,or_greater,suffic:ISO"), "set_auto_exposure_max_sensitivity", "get_auto_exposure_max_sensitivity");
}

CameraAttributesPractical::CameraAttributesPractical() {
	_update_dof_blur();
	_update_exposure();
	set_auto_exposure_min_sensitivity(0.0);
	set_auto_exposure_max_sensitivity(800.0);
	notify_property_list_changed();
}

CameraAttributesPractical::~CameraAttributesPractical() {
}

//////////////////////////////////////////////////////
/* CameraAttributesPhysical */

void CameraAttributesPhysical::set_aperture(float p_aperture) {
	exposure_aperture = p_aperture;
	_update_exposure();
	_update_frustum();
	_update_lens(); // Optical vignetting and diffraction follow the aperture.
}

float CameraAttributesPhysical::get_aperture() const {
	return exposure_aperture;
}

void CameraAttributesPhysical::set_shutter_mode(ShutterMode p_mode) {
	exposure_shutter_mode = p_mode;
	_update_exposure();
	_update_motion_blur();
	notify_property_list_changed();
	emit_changed();
}

CameraAttributesPhysical::ShutterMode CameraAttributesPhysical::get_shutter_mode() const {
	return exposure_shutter_mode;
}

float CameraAttributesPhysical::_get_exposure_time() const {
	if (exposure_shutter_mode == SHUTTER_MODE_ANGLE) {
		// An angle is a share of the camera's frame; without a frame rate set, that of 24 fps film.
		const float fps = frame_rate > 0.0f ? frame_rate : 24.0f;
		return CLAMP(motion_blur_shutter_angle, 1.0f, 360.0f) / 360.0f / fps;
	}
	return exposure_shutter_speed > 0.0f ? 1.0f / exposure_shutter_speed : 0.0f;
}

void CameraAttributesPhysical::set_shutter_speed(float p_shutter_speed) {
	exposure_shutter_speed = p_shutter_speed;
	_update_exposure();
	_update_motion_blur(); // The exposure time sets the length of the blur.
}

float CameraAttributesPhysical::get_shutter_speed() const {
	// In Angle mode, the speed the angle exposes for (shown read-only).
	if (exposure_shutter_mode == SHUTTER_MODE_ANGLE) {
		const float t = _get_exposure_time();
		return t > 0.0f ? 1.0f / t : exposure_shutter_speed;
	}
	return exposure_shutter_speed;
}

void CameraAttributesPhysical::set_motion_blur_use_shutter_speed(bool p_enable) {
	motion_blur_use_shutter_speed = p_enable;
	_update_motion_blur();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributesPhysical::is_motion_blur_using_shutter_speed() const {
	return motion_blur_use_shutter_speed;
}

float CameraAttributesPhysical::_get_motion_blur_shutter_time() const {
	// Angle mode: the exposure time the angle sets, which also sets the brightness.
	if (exposure_shutter_mode == SHUTTER_MODE_ANGLE) {
		return _get_exposure_time();
	}
	return motion_blur_use_shutter_speed && exposure_shutter_speed > 0.0f ? 1.0f / exposure_shutter_speed : 0.0f;
}

void CameraAttributesPhysical::set_focal_length(float p_focal_length) {
	frustum_focal_length = p_focal_length;
	_update_frustum();
	_update_lens();
	emit_changed();
}

float CameraAttributesPhysical::get_focal_length() const {
	return frustum_focal_length;
}

void CameraAttributesPhysical::set_focus_distance(float p_focus_distance) {
	frustum_focus_distance = p_focus_distance;
	_update_frustum();
	_update_lens();
	if (lens_focus_breathing != 0.0f) {
		emit_changed(); // Focus breathing changes the field of view.
	}
}

float CameraAttributesPhysical::get_focus_distance() const {
	return frustum_focus_distance;
}

void CameraAttributesPhysical::set_near(real_t p_near) {
	frustum_near = p_near;
	_update_frustum();
	emit_changed();
}

real_t CameraAttributesPhysical::get_near() const {
	return frustum_near;
}

void CameraAttributesPhysical::set_far(real_t p_far) {
	frustum_far = p_far;
	_update_frustum();
	emit_changed();
}

real_t CameraAttributesPhysical::get_far() const {
	return frustum_far;
}

void CameraAttributesPhysical::set_dof_physical_blur_size(bool p_enable) {
	dof_physical_blur_size = p_enable;
	_update_frustum();
	notify_property_list_changed(); // dof_legacy_gather only shows with it.
	emit_changed();
}

bool CameraAttributesPhysical::is_dof_physical_blur_size() const {
	return dof_physical_blur_size;
}

void CameraAttributesPhysical::set_dof_legacy_gather(bool p_enable) {
	dof_legacy_gather = p_enable;
	RS::get_singleton()->camera_attributes_set_dof_legacy_gather(get_rid(), dof_legacy_gather);
	emit_changed();
}

bool CameraAttributesPhysical::is_dof_legacy_gather() const {
	return dof_legacy_gather;
}

real_t CameraAttributesPhysical::get_fov() const {
	return frustum_fov;
}

bool CameraAttributesPhysical::is_fov_horizontal() const {
	return lens_anamorphic_squeeze != 1.0f;
}

// Focus breathing: the field of view a lens shows changes as it focuses. A lens that focuses by moving as a whole
// ("unit focusing", most classic stills primes) sits at the image distance v = f u / (u - f) from the sensor, so it
// frames like a lens of focal length v: f + f^2 / (u - f) (thin lens). lens_focus_breathing scales that extra length:
// 1 is unit focusing, 0 a cine lens built not to breathe, negative values an internal-focusing lens whose field of
// view widens at close focus (common in modern zooms).
float CameraAttributesPhysical::_get_fov_focal_length() const {
	if (lens_focus_breathing == 0.0f) {
		return frustum_focal_length;
	}
	const float u = MAX(frustum_focus_distance * 1000.0f, frustum_focal_length + 1.0f); // In mm, as in _update_frustum().
	return MAX(frustum_focal_length + lens_focus_breathing * frustum_focal_length * frustum_focal_length / (u - frustum_focal_length), 0.1f);
}

void CameraAttributesPhysical::_update_frustum() {
	//https://en.wikipedia.org/wiki/Circle_of_confusion#Circle_of_confusion_diameter_limit_based_on_d/1500
	// The diagonal in double precision, as the former integer 36x24 mm constant was, so a full-frame sensor gives the
	// same circle of confusion to the bit.
	float CoC = Math::sqrt(double(sensor_size.x) * double(sensor_size.x) + double(sensor_size.y) * double(sensor_size.y)) / 1500.0;

	// Camera3D keeps the vertical field of view the sensor height gives, or for an anamorphic lens the horizontal one
	// of its desqueezed width: the squeeze widens the horizontal field by its factor at the same focal length.
	const float fov_focal_length = _get_fov_focal_length();
	if (is_fov_horizontal()) {
		frustum_fov = Math::rad_to_deg(2 * std::atan(lens_anamorphic_squeeze * sensor_size.width / (2 * fov_focal_length)));
	} else {
		frustum_fov = Math::rad_to_deg(2 * std::atan(sensor_size.height / (2 * fov_focal_length)));
	}

	// Based on https://en.wikipedia.org/wiki/Depth_of_field.
	float u = MAX(frustum_focus_distance * 1000.0, frustum_focal_length + 1.0); // Focus distance expressed in mm and clamped to at least 1 mm away from lens.
	float hyperfocal_length = frustum_focal_length + ((frustum_focal_length * frustum_focal_length) / (exposure_aperture * CoC));

	// This computes the start and end of the depth of field. Anything between these two points has a Circle of Confusino so small
	// that it is not picked up by the camera sensors.
	// To be properly physically-based, we would run the DoF shader at all depths. To be efficient, we are only running it where the CoC
	// will be visible, this introduces some value shifts in the near field that we have to compensate for below.
	float depth_near = ((hyperfocal_length * u) / (hyperfocal_length + (u - frustum_focal_length))) / 1000.0; // In meters.
	float depth_far = ((hyperfocal_length * u) / (hyperfocal_length - (u - frustum_focal_length))) / 1000.0; // In meters.
	float scale = (frustum_focal_length / (u - frustum_focal_length)) * (frustum_focal_length / exposure_aperture);

	bool use_far = (depth_far < frustum_far) && (depth_far > 0.0);
	bool use_near = depth_near > frustum_near;
#ifdef DEBUG_ENABLED
	if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility") {
		// Force disable DoF in editor builds to suppress warnings.
		use_far = false;
		use_near = false;
	}
#endif
	RS::get_singleton()->camera_attributes_set_dof_blur(
			get_rid(),
			use_far,
			u / 1000.0, // Focus distance clampd to focal length expressed in meters.
			-1.0, // Negative to tell Bokeh effect to use physically-based scaling.
			use_near,
			u / 1000.0,
			-1.0,
			scale / 5.0); // Arbitrary scaling to get close to how much blur there should be.

	// Physical blur size. `scale` is the thin lens's blur circle diameter on the sensor for a point at infinity,
	// c = f^2 / (N (u - f)) in mm (https://en.wikipedia.org/wiki/Depth_of_field#Foreground_and_background_blur); the DOF shader's
	// physical mode already scales it by |d - u| / d for a point at distance d. Sent as a fraction of the frame
	// dimension the field of view is fitted to (the sensor height, or for an anamorphic lens its desqueezed width,
	// as in the field of view above), so the renderer can turn it into pixels of the buffer it blurs.
	const bool frame_horizontal = is_fov_horizontal();
	const float frame_mm = frame_horizontal ? lens_anamorphic_squeeze * sensor_size.width : sensor_size.height;
	RS::get_singleton()->camera_attributes_set_dof_physical_blur_size(get_rid(), dof_physical_blur_size, scale / frame_mm, frame_horizontal);
}

float CameraAttributesPhysical::calculate_exposure_normalization() const {
	const float exposure_time = _get_exposure_time();
	float e = (exposure_aperture * exposure_aperture) * (exposure_time > 0.0f ? 1.0f / exposure_time : exposure_shutter_speed) * (100.0 / exposure_sensitivity);
	// An ND filter and the lens's transmission loss (the gap between its T-stop and f-stop) both take light away
	// before the sensor, one stop halving it. Skipped at 0 so the default stays bit-identical.
	const float light_loss_stops = exposure_nd_filter + lens_transmission_loss;
	if (light_loss_stops != 0.0f) {
		e *= std::exp2(light_loss_stops);
	}
	return 1.0 / (e * 1.2);
}

Vector2 CameraAttributesPhysical::get_sensor_format_size(SensorFormat p_format) {
	// Image area in millimeters. Full frame: 135 film. APS-C: Nikon/Sony/Fujifilm DX-size (Canon's is 22.3x14.9).
	// Super 35: ARRI ALEXA open gate's 3:4-ish Super 35 (24.89x18.66, the 4-perf Super 35 film gate). Micro Four
	// Thirds: the standard's 17.3x13. Medium format: the 44x33 "crop" medium format of Fujifilm GFX and Hasselblad X.
	// 1-inch: the 13.2x8.8 type of most premium compacts. (Manufacturer specifications.)
	switch (p_format) {
		case SENSOR_FORMAT_APS_C:
			return Vector2(23.6, 15.6);
		case SENSOR_FORMAT_SUPER_35:
			return Vector2(24.89, 18.66);
		case SENSOR_FORMAT_MICRO_FOUR_THIRDS:
			return Vector2(17.3, 13.0);
		case SENSOR_FORMAT_MEDIUM_FORMAT:
			return Vector2(44.0, 33.0);
		case SENSOR_FORMAT_ONE_INCH:
			return Vector2(13.2, 8.8);
		default:
			return Vector2(36.0, 24.0);
	}
}

void CameraAttributesPhysical::set_sensor_format(SensorFormat p_format) {
	ERR_FAIL_INDEX(p_format, SENSOR_FORMAT_MAX);
	sensor_format = p_format;
	if (sensor_format != SENSOR_FORMAT_CUSTOM) {
		sensor_size = get_sensor_format_size(sensor_format);
	}
	_update_frustum();
	_update_lens();
	notify_property_list_changed();
	emit_changed();
}

CameraAttributesPhysical::SensorFormat CameraAttributesPhysical::get_sensor_format() const {
	return sensor_format;
}

void CameraAttributesPhysical::set_sensor_size(const Vector2 &p_size) {
	sensor_size = Vector2(MAX(p_size.x, 1.0f), MAX(p_size.y, 1.0f));
	// A size other than the preset's makes the format custom; the preset's own size (as a scene stores it after the
	// format) keeps the format.
	if (sensor_format != SENSOR_FORMAT_CUSTOM && sensor_size != get_sensor_format_size(sensor_format)) {
		sensor_format = SENSOR_FORMAT_CUSTOM;
		notify_property_list_changed();
	}
	_update_frustum();
	_update_lens();
	emit_changed();
}

Vector2 CameraAttributesPhysical::get_sensor_size() const {
	return sensor_size;
}

void CameraAttributesPhysical::set_lens_max_aperture(float p_aperture) {
	lens_max_aperture = MAX(p_aperture, 0.5f);
	_update_lens();
	emit_changed();
}

float CameraAttributesPhysical::get_lens_max_aperture() const {
	return lens_max_aperture;
}

void CameraAttributesPhysical::set_lens_transmission_loss(float p_stops) {
	lens_transmission_loss = MAX(p_stops, 0.0f);
	_update_exposure();
	emit_changed();
}

float CameraAttributesPhysical::get_lens_transmission_loss() const {
	return lens_transmission_loss;
}

void CameraAttributesPhysical::set_lens_anamorphic_squeeze(float p_squeeze) {
	lens_anamorphic_squeeze = CLAMP(p_squeeze, 1.0f, 4.0f);
	_update_frustum();
	_update_lens();
	emit_changed();
}

float CameraAttributesPhysical::get_lens_anamorphic_squeeze() const {
	return lens_anamorphic_squeeze;
}

void CameraAttributesPhysical::set_lens_focus_breathing(float p_amount) {
	lens_focus_breathing = p_amount;
	_update_frustum();
	_update_lens();
	emit_changed();
}

float CameraAttributesPhysical::get_lens_focus_breathing() const {
	return lens_focus_breathing;
}

void CameraAttributesPhysical::set_exposure_nd_filter(float p_stops) {
	exposure_nd_filter = MAX(p_stops, 0.0f);
	_update_exposure();
	emit_changed();
}

float CameraAttributesPhysical::get_exposure_nd_filter() const {
	return exposure_nd_filter;
}

void CameraAttributesPhysical::set_diffraction_enabled(bool p_enabled) {
	diffraction_enabled = p_enabled;
	_update_diffraction();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributesPhysical::is_diffraction_enabled() const {
	return diffraction_enabled;
}

void CameraAttributesPhysical::set_diffraction_strength(float p_strength) {
	diffraction_strength = MAX(p_strength, 0.0f);
	_update_diffraction();
	emit_changed();
}

float CameraAttributesPhysical::get_diffraction_strength() const {
	return diffraction_strength;
}

void CameraAttributesPhysical::_update_lens() {
	RS::get_singleton()->camera_attributes_set_lens(get_rid(), sensor_size, frustum_focal_length, _get_fov_focal_length(), frustum_focus_distance, exposure_aperture, lens_max_aperture, lens_anamorphic_squeeze);
}

void CameraAttributesPhysical::_update_diffraction() {
	RS::get_singleton()->camera_attributes_set_diffraction(get_rid(), diffraction_enabled, diffraction_strength);
}

void CameraAttributesPhysical::set_tilt_shift_enabled(bool p_enabled) {
	tilt_shift_enabled = p_enabled;
	_update_tilt_shift();
	notify_property_list_changed();
	emit_changed();
}

bool CameraAttributesPhysical::is_tilt_shift_enabled() const {
	return tilt_shift_enabled;
}

void CameraAttributesPhysical::set_tilt_shift_tilt(float p_degrees) {
	// Tilt-shift lenses tilt up to 8-15 degrees; past 45 the plane of focus lies along the line of sight.
	tilt_shift_tilt = CLAMP(p_degrees, -45.0f, 45.0f);
	_update_tilt_shift();
	emit_changed();
}

float CameraAttributesPhysical::get_tilt_shift_tilt() const {
	return tilt_shift_tilt;
}

void CameraAttributesPhysical::set_tilt_shift_axis(float p_degrees) {
	tilt_shift_axis = p_degrees;
	_update_tilt_shift();
	emit_changed();
}

float CameraAttributesPhysical::get_tilt_shift_axis() const {
	return tilt_shift_axis;
}

void CameraAttributesPhysical::_update_tilt_shift() {
	RS::get_singleton()->camera_attributes_set_tilt_shift(get_rid(), tilt_shift_enabled, tilt_shift_tilt, tilt_shift_axis);
}

void CameraAttributesPhysical::set_auto_exposure_min_exposure_value(float p_min) {
	auto_exposure_min = p_min;
	_update_auto_exposure();
}

float CameraAttributesPhysical::get_auto_exposure_min_exposure_value() const {
	return auto_exposure_min;
}

void CameraAttributesPhysical::set_auto_exposure_max_exposure_value(float p_max) {
	auto_exposure_max = p_max;
	_update_auto_exposure();
}

float CameraAttributesPhysical::get_auto_exposure_max_exposure_value() const {
	return auto_exposure_max;
}

void CameraAttributesPhysical::_update_auto_exposure() {
	RS::get_singleton()->camera_attributes_set_auto_exposure(
			get_rid(),
			auto_exposure_enabled,
			std::pow(2.0, auto_exposure_min) * (12.5 / exposure_sensitivity), // Convert from EV100 to Luminance
			std::pow(2.0, auto_exposure_max) * (12.5 / exposure_sensitivity), // Convert from EV100 to Luminance
			auto_exposure_speed,
			auto_exposure_scale);
	emit_changed();
}

void CameraAttributesPhysical::_validate_property(PropertyInfo &property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// The shutter speed sets the motion blur's exposure time too, so it shows whenever that uses it.
	const bool angle_mode = exposure_shutter_mode == SHUTTER_MODE_ANGLE;
	if (angle_mode && property.name == "exposure_shutter_speed") {
		property.usage |= PROPERTY_USAGE_READ_ONLY;
	}
	if (angle_mode && property.name == "motion_blur_use_shutter_speed") {
		property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	const bool shutter_speed_blurs = motion_blur_enabled && motion_blur_use_shutter_speed && !angle_mode;
	if (!GLOBAL_GET_CACHED(bool, "rendering/lights_and_shadows/use_physical_light_units") && (property.name == "exposure_aperture" || (property.name == "exposure_shutter_speed" && !shutter_speed_blurs) || property.name == "exposure_nd_filter")) {
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
	}
	if (property.name == "exposure_shutter_angle" && motion_blur_use_shutter_speed && !angle_mode) {
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
	}
	// The physical vignetting has no free falloff, and automatic distortion has no coefficients to set.
	if (property.name == "vignetting_falloff" || ((property.name == "distortion_k1" || property.name == "distortion_k2") && distortion_automatic)) {
		property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (property.name == "sensor_size" && sensor_format != SENSOR_FORMAT_CUSTOM) {
		property.usage |= PROPERTY_USAGE_READ_ONLY;
	}
	if (property.name == "diffraction_strength" && !diffraction_enabled) {
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
	}
	if ((property.name == "tilt_shift_tilt" || property.name == "tilt_shift_axis") && !tilt_shift_enabled) {
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
	}
	// Without the thin-lens size the gather already is upstream's.
	if (property.name == "dof_legacy_gather" && !dof_physical_blur_size) {
		property.usage = PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL;
	}
}

void CameraAttributesPhysical::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_aperture", "aperture"), &CameraAttributesPhysical::set_aperture);
	ClassDB::bind_method(D_METHOD("get_aperture"), &CameraAttributesPhysical::get_aperture);
	ClassDB::bind_method(D_METHOD("set_shutter_speed", "shutter_speed"), &CameraAttributesPhysical::set_shutter_speed);
	ClassDB::bind_method(D_METHOD("get_shutter_speed"), &CameraAttributesPhysical::get_shutter_speed);
	ClassDB::bind_method(D_METHOD("set_shutter_mode", "mode"), &CameraAttributesPhysical::set_shutter_mode);
	ClassDB::bind_method(D_METHOD("get_shutter_mode"), &CameraAttributesPhysical::get_shutter_mode);
	ClassDB::bind_method(D_METHOD("set_motion_blur_use_shutter_speed", "enable"), &CameraAttributesPhysical::set_motion_blur_use_shutter_speed);
	ClassDB::bind_method(D_METHOD("is_motion_blur_using_shutter_speed"), &CameraAttributesPhysical::is_motion_blur_using_shutter_speed);

	ClassDB::bind_method(D_METHOD("set_focal_length", "focal_length"), &CameraAttributesPhysical::set_focal_length);
	ClassDB::bind_method(D_METHOD("get_focal_length"), &CameraAttributesPhysical::get_focal_length);
	ClassDB::bind_method(D_METHOD("set_focus_distance", "focus_distance"), &CameraAttributesPhysical::set_focus_distance);
	ClassDB::bind_method(D_METHOD("get_focus_distance"), &CameraAttributesPhysical::get_focus_distance);
	ClassDB::bind_method(D_METHOD("set_near", "near"), &CameraAttributesPhysical::set_near);
	ClassDB::bind_method(D_METHOD("get_near"), &CameraAttributesPhysical::get_near);
	ClassDB::bind_method(D_METHOD("set_far", "far"), &CameraAttributesPhysical::set_far);
	ClassDB::bind_method(D_METHOD("get_far"), &CameraAttributesPhysical::get_far);
	ClassDB::bind_method(D_METHOD("get_fov"), &CameraAttributesPhysical::get_fov);

	ClassDB::bind_method(D_METHOD("set_dof_physical_blur_size", "enable"), &CameraAttributesPhysical::set_dof_physical_blur_size);
	ClassDB::bind_method(D_METHOD("is_dof_physical_blur_size"), &CameraAttributesPhysical::is_dof_physical_blur_size);
	ClassDB::bind_method(D_METHOD("set_dof_legacy_gather", "enable"), &CameraAttributesPhysical::set_dof_legacy_gather);
	ClassDB::bind_method(D_METHOD("is_dof_legacy_gather"), &CameraAttributesPhysical::is_dof_legacy_gather);

	ClassDB::bind_method(D_METHOD("set_auto_exposure_max_exposure_value", "exposure_value_max"), &CameraAttributesPhysical::set_auto_exposure_max_exposure_value);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_max_exposure_value"), &CameraAttributesPhysical::get_auto_exposure_max_exposure_value);
	ClassDB::bind_method(D_METHOD("set_auto_exposure_min_exposure_value", "exposure_value_min"), &CameraAttributesPhysical::set_auto_exposure_min_exposure_value);
	ClassDB::bind_method(D_METHOD("get_auto_exposure_min_exposure_value"), &CameraAttributesPhysical::get_auto_exposure_min_exposure_value);

	ClassDB::bind_method(D_METHOD("is_fov_horizontal"), &CameraAttributesPhysical::is_fov_horizontal);

	ClassDB::bind_method(D_METHOD("set_sensor_format", "format"), &CameraAttributesPhysical::set_sensor_format);
	ClassDB::bind_method(D_METHOD("get_sensor_format"), &CameraAttributesPhysical::get_sensor_format);
	ClassDB::bind_method(D_METHOD("set_sensor_size", "size"), &CameraAttributesPhysical::set_sensor_size);
	ClassDB::bind_method(D_METHOD("get_sensor_size"), &CameraAttributesPhysical::get_sensor_size);
	ClassDB::bind_static_method("CameraAttributesPhysical", D_METHOD("get_sensor_format_size", "format"), &CameraAttributesPhysical::get_sensor_format_size);

	ClassDB::bind_method(D_METHOD("set_lens_max_aperture", "aperture"), &CameraAttributesPhysical::set_lens_max_aperture);
	ClassDB::bind_method(D_METHOD("get_lens_max_aperture"), &CameraAttributesPhysical::get_lens_max_aperture);
	ClassDB::bind_method(D_METHOD("set_lens_transmission_loss", "stops"), &CameraAttributesPhysical::set_lens_transmission_loss);
	ClassDB::bind_method(D_METHOD("get_lens_transmission_loss"), &CameraAttributesPhysical::get_lens_transmission_loss);
	ClassDB::bind_method(D_METHOD("set_lens_anamorphic_squeeze", "squeeze"), &CameraAttributesPhysical::set_lens_anamorphic_squeeze);
	ClassDB::bind_method(D_METHOD("get_lens_anamorphic_squeeze"), &CameraAttributesPhysical::get_lens_anamorphic_squeeze);
	ClassDB::bind_method(D_METHOD("set_lens_focus_breathing", "amount"), &CameraAttributesPhysical::set_lens_focus_breathing);
	ClassDB::bind_method(D_METHOD("get_lens_focus_breathing"), &CameraAttributesPhysical::get_lens_focus_breathing);

	ClassDB::bind_method(D_METHOD("set_exposure_nd_filter", "stops"), &CameraAttributesPhysical::set_exposure_nd_filter);
	ClassDB::bind_method(D_METHOD("get_exposure_nd_filter"), &CameraAttributesPhysical::get_exposure_nd_filter);

	ClassDB::bind_method(D_METHOD("set_diffraction_enabled", "enabled"), &CameraAttributesPhysical::set_diffraction_enabled);
	ClassDB::bind_method(D_METHOD("is_diffraction_enabled"), &CameraAttributesPhysical::is_diffraction_enabled);
	ClassDB::bind_method(D_METHOD("set_diffraction_strength", "strength"), &CameraAttributesPhysical::set_diffraction_strength);
	ClassDB::bind_method(D_METHOD("get_diffraction_strength"), &CameraAttributesPhysical::get_diffraction_strength);

	ClassDB::bind_method(D_METHOD("set_tilt_shift_enabled", "enabled"), &CameraAttributesPhysical::set_tilt_shift_enabled);
	ClassDB::bind_method(D_METHOD("is_tilt_shift_enabled"), &CameraAttributesPhysical::is_tilt_shift_enabled);
	ClassDB::bind_method(D_METHOD("set_tilt_shift_tilt", "degrees"), &CameraAttributesPhysical::set_tilt_shift_tilt);
	ClassDB::bind_method(D_METHOD("get_tilt_shift_tilt"), &CameraAttributesPhysical::get_tilt_shift_tilt);
	ClassDB::bind_method(D_METHOD("set_tilt_shift_axis", "degrees"), &CameraAttributesPhysical::set_tilt_shift_axis);
	ClassDB::bind_method(D_METHOD("get_tilt_shift_axis"), &CameraAttributesPhysical::get_tilt_shift_axis);

	ADD_GROUP("Sensor", "sensor_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sensor_format", PROPERTY_HINT_ENUM, "Full Frame (36x24 mm),APS-C (23.6x15.6 mm),Super 35 (24.89x18.66 mm),Micro Four Thirds (17.3x13 mm),Medium Format (44x33 mm),1-inch (13.2x8.8 mm),Custom"), "set_sensor_format", "get_sensor_format");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "sensor_size", PROPERTY_HINT_NONE, "suffix:mm"), "set_sensor_size", "get_sensor_size");

	ADD_GROUP("Lens", "lens_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lens_max_aperture", PROPERTY_HINT_RANGE, "0.5,32.0,0.01,exp,suffix:f-stop"), "set_lens_max_aperture", "get_lens_max_aperture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lens_transmission_loss", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:stops"), "set_lens_transmission_loss", "get_lens_transmission_loss");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lens_anamorphic_squeeze", PROPERTY_HINT_RANGE, "1,2,0.01,or_greater"), "set_lens_anamorphic_squeeze", "get_lens_anamorphic_squeeze");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lens_focus_breathing", PROPERTY_HINT_RANGE, "-1,1,0.01,or_greater,or_less"), "set_lens_focus_breathing", "get_lens_focus_breathing");

	ADD_GROUP("Diffraction", "diffraction_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "diffraction_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_diffraction_enabled", "is_diffraction_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "diffraction_strength", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_diffraction_strength", "get_diffraction_strength");

	ADD_GROUP("Tilt-Shift", "tilt_shift_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tilt_shift_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_tilt_shift_enabled", "is_tilt_shift_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tilt_shift_tilt", PROPERTY_HINT_RANGE, "-15,15,0.01,or_greater,or_less,degrees"), "set_tilt_shift_tilt", "get_tilt_shift_tilt");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tilt_shift_axis", PROPERTY_HINT_RANGE, "-180,180,0.1,degrees"), "set_tilt_shift_axis", "get_tilt_shift_axis");

	ADD_GROUP("Frustum", "frustum_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frustum_focus_distance", PROPERTY_HINT_RANGE, "0.01,4000.0,0.01,suffix:m"), "set_focus_distance", "get_focus_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frustum_focal_length", PROPERTY_HINT_RANGE, "1.0,800.0,0.01,exp,suffix:mm"), "set_focal_length", "get_focal_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frustum_near", PROPERTY_HINT_RANGE, "0.001,10,0.001,or_greater,exp,suffix:m"), "set_near", "get_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frustum_far", PROPERTY_HINT_RANGE, "0.01,4000,0.01,or_greater,exp,suffix:m"), "set_far", "get_far");

	ADD_GROUP("Depth of Field", "dof_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_physical_blur_size"), "set_dof_physical_blur_size", "is_dof_physical_blur_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dof_legacy_gather"), "set_dof_legacy_gather", "is_dof_legacy_gather");

	ADD_GROUP("Exposure", "exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_aperture", PROPERTY_HINT_RANGE, "0.5,64.0,0.01,exp,suffix:f-stop"), "set_aperture", "get_aperture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "exposure_shutter_mode", PROPERTY_HINT_ENUM, "Speed,Angle"), "set_shutter_mode", "get_shutter_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_shutter_speed", PROPERTY_HINT_RANGE, "0.1,8000.0,0.001,suffix:1/s"), "set_shutter_speed", "get_shutter_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure_nd_filter", PROPERTY_HINT_RANGE, "0,10,0.1,or_greater,suffix:stops"), "set_exposure_nd_filter", "get_exposure_nd_filter");

	ADD_GROUP("Auto Exposure", "auto_exposure_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_min_exposure_value", PROPERTY_HINT_RANGE, "-16.0,16.0,0.01,or_greater,suffix:EV100"), "set_auto_exposure_min_exposure_value", "get_auto_exposure_min_exposure_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "auto_exposure_max_exposure_value", PROPERTY_HINT_RANGE, "-16.0,16.0,0.01,or_greater,suffix:EV100"), "set_auto_exposure_max_exposure_value", "get_auto_exposure_max_exposure_value");

	ADD_GROUP("Motion Blur", "motion_blur_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "motion_blur_use_shutter_speed"), "set_motion_blur_use_shutter_speed", "is_motion_blur_using_shutter_speed");

	BIND_ENUM_CONSTANT(SENSOR_FORMAT_FULL_FRAME);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_APS_C);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_SUPER_35);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_MICRO_FOUR_THIRDS);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_MEDIUM_FORMAT);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_ONE_INCH);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_CUSTOM);
	BIND_ENUM_CONSTANT(SENSOR_FORMAT_MAX);

	BIND_ENUM_CONSTANT(SHUTTER_MODE_SPEED);
	BIND_ENUM_CONSTANT(SHUTTER_MODE_ANGLE);
}

CameraAttributesPhysical::CameraAttributesPhysical() {
	_update_exposure();
	_update_frustum();
	_update_motion_blur(); // The base constructor cannot reach _get_motion_blur_shutter_time()'s override.
	_update_lens();
	set_auto_exposure_min_exposure_value(-8);
	set_auto_exposure_max_exposure_value(10); // Use a wide range by default to feel more like a real camera.
	notify_property_list_changed();
}

CameraAttributesPhysical::~CameraAttributesPhysical() {
}
