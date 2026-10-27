/**************************************************************************/
/*  camera_attributes_storage.cpp                                         */
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

#include "camera_attributes_storage.h"

#ifdef DEBUG_ENABLED
#include "core/os/os.h"
#endif

RendererCameraAttributes *RendererCameraAttributes::singleton = nullptr;
uint64_t RendererCameraAttributes::auto_exposure_counter = 2;

RendererCameraAttributes::RendererCameraAttributes() {
	singleton = this;
}

RendererCameraAttributes::~RendererCameraAttributes() {
	singleton = nullptr;
}

RID RendererCameraAttributes::camera_attributes_allocate() {
	return camera_attributes_owner.allocate_rid();
}

void RendererCameraAttributes::camera_attributes_initialize(RID p_rid) {
	camera_attributes_owner.initialize_rid(p_rid, CameraAttributes());
}

void RendererCameraAttributes::camera_attributes_free(RID p_rid) {
	camera_attributes_owner.free(p_rid);
}

void RendererCameraAttributes::camera_attributes_set_dof_blur_quality(RSE::DOFBlurQuality p_quality, bool p_use_jitter) {
	dof_blur_quality = p_quality;
	dof_blur_use_jitter = p_use_jitter;
}

void RendererCameraAttributes::camera_attributes_set_dof_blur_bokeh_shape(RSE::DOFBokehShape p_shape) {
	dof_blur_bokeh_shape = p_shape;
}

void RendererCameraAttributes::camera_attributes_set_dof_blur(RID p_camera_attributes, bool p_far_enable, float p_far_distance, float p_far_transition, bool p_near_enable, float p_near_distance, float p_near_transition, float p_amount) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
#ifdef DEBUG_ENABLED
	if (OS::get_singleton()->get_current_rendering_method() == "gl_compatibility" && (p_far_enable || p_near_enable)) {
		WARN_PRINT_ONCE_ED("Depth of field blur is only available when using the Forward+ or Mobile renderer.");
	}
#endif
	cam_attributes->dof_blur_far_enabled = p_far_enable;
	cam_attributes->dof_blur_far_distance = p_far_distance;
	cam_attributes->dof_blur_far_transition = p_far_transition;

	cam_attributes->dof_blur_near_enabled = p_near_enable;
	cam_attributes->dof_blur_near_distance = p_near_distance;
	cam_attributes->dof_blur_near_transition = p_near_transition;

	cam_attributes->dof_blur_amount = p_amount;
}

bool RendererCameraAttributes::camera_attributes_get_dof_far_enabled(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, false);
	return cam_attributes->dof_blur_far_enabled;
}

float RendererCameraAttributes::camera_attributes_get_dof_far_distance(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->dof_blur_far_distance;
}

float RendererCameraAttributes::camera_attributes_get_dof_far_transition(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->dof_blur_far_transition;
}

bool RendererCameraAttributes::camera_attributes_get_dof_near_enabled(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, false);
	return cam_attributes->dof_blur_near_enabled;
}

float RendererCameraAttributes::camera_attributes_get_dof_near_distance(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->dof_blur_near_distance;
}

float RendererCameraAttributes::camera_attributes_get_dof_near_transition(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->dof_blur_near_transition;
}

float RendererCameraAttributes::camera_attributes_get_dof_blur_amount(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->dof_blur_amount;
}

void RendererCameraAttributes::camera_attributes_set_exposure(RID p_camera_attributes, float p_multiplier, float p_exposure_normalization) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->exposure_multiplier = p_multiplier;
	cam_attributes->exposure_normalization = p_exposure_normalization;
}

float RendererCameraAttributes::camera_attributes_get_exposure_normalization_factor(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 1.0);

	return cam_attributes->exposure_multiplier * cam_attributes->exposure_normalization;
}

void RendererCameraAttributes::camera_attributes_set_auto_exposure(RID p_camera_attributes, bool p_enable, float p_min_sensitivity, float p_max_sensitivity, float p_speed, float p_scale) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	if (!cam_attributes->use_auto_exposure && p_enable) {
		cam_attributes->auto_exposure_version = ++auto_exposure_counter;
	}
#ifdef DEBUG_ENABLED
	if ((OS::get_singleton()->get_current_rendering_method() == "gl_compatibility" || OS::get_singleton()->get_current_rendering_method() == "mobile") && p_enable) {
		WARN_PRINT_ONCE_ED("Auto-exposure is only available when using the Forward+ renderer.");
	}
#endif
	cam_attributes->use_auto_exposure = p_enable;
	cam_attributes->auto_exposure_min_sensitivity = p_min_sensitivity;
	cam_attributes->auto_exposure_max_sensitivity = p_max_sensitivity;
	cam_attributes->auto_exposure_adjust_speed = p_speed;
	cam_attributes->auto_exposure_scale = p_scale;
}

float RendererCameraAttributes::camera_attributes_get_auto_exposure_min_sensitivity(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->auto_exposure_min_sensitivity;
}

float RendererCameraAttributes::camera_attributes_get_auto_exposure_max_sensitivity(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->auto_exposure_max_sensitivity;
}

float RendererCameraAttributes::camera_attributes_get_auto_exposure_adjust_speed(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->auto_exposure_adjust_speed;
}

float RendererCameraAttributes::camera_attributes_get_auto_exposure_scale(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->auto_exposure_scale;
}

uint64_t RendererCameraAttributes::camera_attributes_get_auto_exposure_version(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0);
	return cam_attributes->auto_exposure_version;
}

/* White balance
 *
 * Camera semantics: the temperature is the illuminant the camera is set for. A light of that color comes out the
 * way a 6500 K light does at the default setting, so 3200 K makes a tungsten-lit scene neutral and turns a
 * daylight one blue. The model:
 *
 * - The illuminant's white is on the Planckian locus at the given temperature, in CIE 1960 UCS, using the same
 *   approximation (Krystek 1985) as Light3D::light_temperature, so a light set to T under a white balance of T
 *   renders exactly like the default 6500 K light at the default white balance.
 * - The tint moves that white along the isotherm, perpendicular to the locus, by 1/3000 Duv per unit, so the
 *   -150..150 range (Lightroom-style numbers) spans +-0.05 Duv, the extent of Unreal's WhiteTint at +-1.
 *   Positive tint means the illuminant is greener than the locus, so it is removed and the image turns
 *   magenta, as in Lightroom and most raw converters.
 * - A von Kries adaptation in Bradford cone space (LMS) maps that white onto the model's own 6500 K / tint 0
 *   white, which makes the default exactly the identity rather than "nearly D65". Only the three cone gains
 *   cross to the GPU; the tonemappers apply linear sRGB -> LMS, the gains, and LMS -> linear sRGB with
 *   constant matrices (BRADFORD * SRGB_TO_XYZ and its inverse, spelled out in tonemap.glsl, tonemap_mobile.glsl
 *   and the Compatibility tonemap_inc.glsl), so the full 3x3 never needs push-constant space.
 */

// Duv per unit of tint: +-150 is +-0.05 Duv.
static constexpr double WHITE_BALANCE_DUV_PER_TINT = 0.05 / 150.0;

static void _white_balance_planckian_uv(double p_temperature, double &r_u, double &r_v) {
	const double t = p_temperature;
	const double t2 = t * t;
	r_u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2);
	r_v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2);
}

// Bradford LMS of the white at the given temperature and tint, with Y = 1.
static Vector3 _white_balance_white_lms(double p_temperature, double p_tint) {
	double u, v;
	_white_balance_planckian_uv(p_temperature, u, v);

	if (p_tint != 0.0) {
		// Offset along the isotherm. The locus runs toward smaller u as the temperature rises, so (dv, -du)
		// is the normal that points to larger v, the green side.
		double u0, v0, u1, v1;
		_white_balance_planckian_uv(p_temperature - 1.0, u0, v0);
		_white_balance_planckian_uv(p_temperature + 1.0, u1, v1);
		const double du = u1 - u0;
		const double dv = v1 - v0;
		const double len = Math::sqrt(du * du + dv * dv);
		if (len > 0.0) {
			const double duv = p_tint * WHITE_BALANCE_DUV_PER_TINT;
			u += dv / len * duv;
			v += -du / len * duv;
		}
	}

	// CIE 1960 uv -> xy -> XYZ.
	const double d = 2.0 * u - 8.0 * v + 4.0;
	const double x = 3.0 * u / d;
	const double y = MAX(2.0 * v / d, 1e-6);
	const double X = x / y;
	const double Y = 1.0;
	const double Z = (1.0 - x - y) / y;

	// Bradford cone response.
	return Vector3(
			0.8951 * X + 0.2664 * Y - 0.1614 * Z,
			-0.7502 * X + 1.7135 * Y + 0.0367 * Z,
			0.0389 * X - 0.0685 * Y + 1.0296 * Z);
}

Vector3 RendererCameraAttributes::white_balance_compute_gains(float p_temperature, float p_tint) {
	const Vector3 reference = _white_balance_white_lms(6500.0, 0.0);
	const Vector3 source = _white_balance_white_lms(CLAMP(p_temperature, 2000.0f, 10000.0f), CLAMP(p_tint, -150.0f, 150.0f));
	return Vector3(reference.x / source.x, reference.y / source.y, reference.z / source.z);
}

void RendererCameraAttributes::camera_attributes_set_white_balance(RID p_camera_attributes, float p_temperature, float p_tint) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->white_balance_temperature = p_temperature;
	cam_attributes->white_balance_tint = p_tint;
	// The exact default is skipped outright, so it costs nothing and leaves the output bit-identical.
	cam_attributes->use_white_balance = p_temperature != 6500.0f || p_tint != 0.0f;
	cam_attributes->white_balance_gains = cam_attributes->use_white_balance ? white_balance_compute_gains(p_temperature, p_tint) : Vector3(1.0, 1.0, 1.0);
}

float RendererCameraAttributes::camera_attributes_get_white_balance_temperature(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 6500.0);
	return cam_attributes->white_balance_temperature;
}

float RendererCameraAttributes::camera_attributes_get_white_balance_tint(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, 0.0);
	return cam_attributes->white_balance_tint;
}

Vector3 RendererCameraAttributes::camera_attributes_get_white_balance_gains(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, Vector3(1.0, 1.0, 1.0));
	return cam_attributes->white_balance_gains;
}

/* Grain
 *
 * Film grain and digital sensor noise from the camera's ISO, worked out per tonemap pass so the image looks equally
 * grainy at any output resolution. Both models put the frame on the 36x24 mm format CameraAttributesPhysical uses
 * for its field of view, so an output pixel covers 24 mm / height of it.
 *
 * Resolution: noise that is finer than a pixel averages out inside it, and Selwyn's law (the RMS fluctuation times
 * the square root of the measuring area is constant) says by how much. The shader is given the grain's own
 * amplitude and the size of one grain "cell" in output pixels: cells smaller than a pixel give white noise scaled
 * by the cell size, larger ones give cellular noise, one value per grain at full amplitude. Lower
 * resolutions therefore show weaker but proportionally coarser noise per pixel, which the eye reads as the same
 * grain. grain_size scales the cell while keeping each cell's amplitude, so larger grain is also more visible.
 *
 * Film: Kodak's diffuse RMS granularity (density fluctuation x 1000, read through a 48 um aperture) runs 8 for
 * ISO 100 emulsions, 10-11 for ISO 400 and 18 for ISO 3200 (T-Max 100, 400 and P3200; Portra 400 is 11), which
 * fits 8 * (ISO / 100)^0.23. That is the granularity at a density of 1, near middle gray on a negative; the shader
 * grows it with the square root of the local density. Grain clumps are taken as 6 um at ISO 100, growing with
 * (ISO / 100)^0.4 (faster emulsions have larger crystals). A density fluctuation becomes a fluctuation of log
 * exposure through the negative's gamma of 0.6, so the shader applies it as an exposure change in stops before
 * the tonemapper, which plays the print and compresses it in the highlights.
 *
 * Digital: a full-frame sensor with 6 um photosites (24 MP) holding 1400 electrons per um^2 at ISO 100 (50,000
 * per photosite, a typical full well), halved per doubling of ISO. Scene value 1.0 is taken as saturation, which
 * is what the saturation-based exposure normalization of CameraAttributesPhysical produces. Shot noise is the
 * square root of the electron count; read noise is 1.5 electrons before the ISO gain plus 8 electrons after it,
 * referred to the input, so it dominates the shadows at low ISO and fades at high ISO as on real sensors. The
 * shader adds the noise in linear light per Bayer channel. At ISO 100 and 1080p this is far below anything
 * visible, as it is on a real camera; it shows from about ISO 1600, or with grain_intensity.
 */

void RendererCameraAttributes::camera_attributes_set_grain(RID p_camera_attributes, bool p_enable, RSE::CameraAttributesGrainType p_type, float p_intensity, float p_size, float p_sensitivity) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	ERR_FAIL_INDEX((int)p_type, 3);
	cam_attributes->grain_enabled = p_enable;
	cam_attributes->grain_type = p_type;
	cam_attributes->grain_intensity = MAX(p_intensity, 0.0f);
	cam_attributes->grain_size = MAX(p_size, 0.01f);
	cam_attributes->grain_sensitivity = MAX(p_sensitivity, 0.1f);
}

// Quantizes log2(p_value) over [p_lo, p_hi] into p_bits bits.
static uint32_t _grain_encode_log2(double p_value, int p_bits, double p_lo, double p_hi) {
	const uint32_t max_value = (1u << p_bits) - 1u;
	const double t = (Math::log2(MAX(p_value, 1e-30)) - p_lo) / (p_hi - p_lo);
	return (uint32_t)Math::round(CLAMP(t, 0.0, 1.0) * max_value);
}

uint32_t RendererCameraAttributes::camera_attributes_get_grain_packed(RID p_camera_attributes, int p_output_height, uint64_t p_frame) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	if (!cam_attributes || !cam_attributes->grain_enabled || cam_attributes->grain_intensity <= 0.0f || p_output_height <= 0) {
		return 0;
	}

	const double iso_ratio = cam_attributes->grain_sensitivity / 100.0;
	const double pixel_um = 24000.0 / p_output_height; // The output pixel's footprint on a 24 mm tall frame.

	double cell_um; // Grain cell size on the frame at grain_size 1.
	double amplitude_a;
	double amplitude_b = 0.0;
	uint32_t type;
	if (cam_attributes->grain_type == RSE::CAMERA_ATTRIBUTES_GRAIN_DIGITAL) {
		cell_um = 6.0;
		const double full_well = 1400.0 * cell_um * cell_um / iso_ratio; // Electrons at saturation.
		const double read_noise = Math::sqrt(1.5 * 1.5 + (8.0 / iso_ratio) * (8.0 / iso_ratio)); // Electrons.
		amplitude_a = 1.0 / Math::sqrt(full_well); // Shot noise at signal 1.0 (saturation).
		amplitude_b = read_noise / full_well;
		type = 3;
	} else {
		cell_um = 6.0 * Math::pow(iso_ratio, 0.4);
		// Granularity at the 48 um aperture, converted to the grain cell's own area by Selwyn's law, then from
		// density to stops of exposure through the negative's gamma: 0.6 * log10(2) density per stop.
		const double granularity = 0.008 * Math::pow(iso_ratio, 0.23);
		const double aperture_side_um = Math::sqrt(Math::PI) * 24.0; // The 48 um circle's area as a square.
		amplitude_a = granularity * aperture_side_um / cell_um / (0.6 * Math::log(2.0) / Math::log(10.0));
		type = cam_attributes->grain_type == RSE::CAMERA_ATTRIBUTES_GRAIN_FILM_MONOCHROME ? 2 : 1;
	}
	amplitude_a *= cam_attributes->grain_intensity;
	amplitude_b *= cam_attributes->grain_intensity;
	const double cell_px = cell_um * cam_attributes->grain_size / pixel_um;

	// Bit layout: see camera_grain_inc.glsl.
	uint32_t packed = type;
	packed |= _grain_encode_log2(cell_px, 7, -8.0, 5.0) << 2;
	packed |= _grain_encode_log2(amplitude_a, 8, -16.0, 4.0) << 9;
	packed |= _grain_encode_log2(amplitude_b, 7, -16.0, 4.0) << 17;
	packed |= uint32_t(p_frame & 0xFF) << 24;
	return packed;
}

/* LUT
 *
 * A look-up table the camera applies, either to its display image or, for a camera log encoding, in place of the
 * tonemapper; camera_lut_inc.glsl applies it and documents both. Only the input space and the strength cross as
 * parameters; the encodings and gamut matrices are constants in the shader.
 */

void RendererCameraAttributes::camera_attributes_set_lut(RID p_camera_attributes, bool p_enable, RID p_texture, RSE::CameraAttributesLUTInput p_input, float p_strength) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	ERR_FAIL_INDEX((int)p_input, (int)RSE::CAMERA_ATTRIBUTES_LUT_INPUT_MAX);
	cam_attributes->lut_enabled = p_enable;
	cam_attributes->lut_texture = p_texture;
	cam_attributes->lut_input = p_input;
	cam_attributes->lut_strength = CLAMP(p_strength, 0.0f, 1.0f);
}

RID RendererCameraAttributes::camera_attributes_get_lut_texture(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL_V(cam_attributes, RID());
	return cam_attributes->lut_texture;
}

uint32_t RendererCameraAttributes::camera_attributes_get_lut_packed(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	if (!cam_attributes || !cam_attributes->lut_enabled || cam_attributes->lut_texture.is_null() || cam_attributes->lut_strength <= 0.0f) {
		return 0;
	}
	// Bit layout: see camera_lut_inc.glsl.
	uint32_t packed = 1;
	packed |= uint32_t(cam_attributes->lut_input) << 1;
	packed |= uint32_t(Math::round(cam_attributes->lut_strength * 255.0f)) << 8;
	return packed;
}

/* Motion blur and rolling shutter
 *
 * Both come from when the sensor records light, and both are worked out from the motion of the last frame, which the
 * velocity buffer holds per pixel (Forward+ renders it for TAA; the renderer turns it on for these too). A frame's
 * motion covers one frame time, so everything here is a multiple of it.
 *
 * Motion blur: the shutter is open for the exposure time, so a point moving at constant speed is smeared over speed x
 * exposure time. Measured in frames of motion that is exposure time / frame time: a 1/50 s shutter at 60 fps smears
 * 1.2 frames' worth, 1/2000 s 0.03. The exposure time is CameraAttributesPhysical's shutter speed, or a shutter angle:
 * the film-camera convention, where a rotary shutter open for angle / 360 of each frame (180 degrees is the cinema
 * standard, so the blur is half a frame's motion at any frame rate). The exposure is centered on the frame's time,
 * so the blur extends half each way, which is how a frame's moment is defined for a film camera.
 *
 * Rolling shutter: a CMOS sensor read out row by row exposes each row later than the one above it, the readout time
 * apart from top to bottom (typically 5-30 ms on stills and video cameras, 0 for a global shutter). The renderer
 * shows each row as it was readout x (row / height - 0.5) away from the frame's time, centered so that the middle
 * row is unchanged.
 */

void RendererCameraAttributes::camera_attributes_set_motion_blur_quality(RSE::CameraAttributesMotionBlurQuality p_quality) {
	ERR_FAIL_INDEX((int)p_quality, 3);
	motion_blur_quality = p_quality;
}

void RendererCameraAttributes::camera_attributes_set_motion_blur(RID p_camera_attributes, bool p_enable, float p_shutter_angle, float p_shutter_time, float p_strength, float p_max_length) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->motion_blur_enabled = p_enable;
	cam_attributes->motion_blur_shutter_angle = CLAMP(p_shutter_angle, 0.0f, 360.0f);
	cam_attributes->motion_blur_shutter_time = MAX(p_shutter_time, 0.0f);
	cam_attributes->motion_blur_strength = MAX(p_strength, 0.0f);
	cam_attributes->motion_blur_max_length = CLAMP(p_max_length, 0.0f, 1.0f);
}

void RendererCameraAttributes::camera_attributes_set_rolling_shutter(RID p_camera_attributes, bool p_enable, float p_readout_time) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->rolling_shutter_enabled = p_enable;
	cam_attributes->rolling_shutter_readout_time = MAX(p_readout_time, 0.0f);
}

RendererCameraAttributes::ShutterMotion RendererCameraAttributes::camera_attributes_get_shutter_motion(RID p_camera_attributes, double p_frame_time) {
	ShutterMotion motion;
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	if (!cam_attributes) {
		return motion;
	}
	// The frame time is the step the frame's motion was simulated over. A stopped clock (time scale 0) has no motion to
	// measure against; take 60 fps so a camera moved while the game is frozen still blurs as a camera would.
	const double frame_time = p_frame_time > 1e-6 ? p_frame_time : 1.0 / 60.0;

	if (cam_attributes->motion_blur_enabled && cam_attributes->motion_blur_max_length > 0.0f) {
		double exposure_frames;
		if (cam_attributes->motion_blur_shutter_time > 0.0f) {
			exposure_frames = cam_attributes->motion_blur_shutter_time / frame_time;
		} else {
			exposure_frames = cam_attributes->motion_blur_shutter_angle / 360.0;
		}
		motion.blur_scale = exposure_frames * cam_attributes->motion_blur_strength;
		motion.max_length = cam_attributes->motion_blur_max_length;
	}
	if (cam_attributes->rolling_shutter_enabled) {
		motion.rolling_shutter = cam_attributes->rolling_shutter_readout_time * 0.001 / frame_time;
	}
	return motion;
}

uint32_t RendererCameraAttributes::camera_attributes_get_motion_blur_sample_count() const {
	// Taps along the blur per pixel. McGuire et al. (2012) use 15 with per-pixel jitter; Medium is about that.
	switch (motion_blur_quality) {
		case RSE::CAMERA_ATTRIBUTES_MOTION_BLUR_QUALITY_LOW:
			return 8;
		case RSE::CAMERA_ATTRIBUTES_MOTION_BLUR_QUALITY_HIGH:
			return 32;
		default:
			return 16;
	}
}
