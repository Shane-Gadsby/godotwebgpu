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
 * grainy at any output resolution. Both models put the frame on the sensor CameraAttributesPhysical uses for its
 * field of view (36x24 mm unless its sensor format says otherwise), so an output pixel covers sensor height / height
 * of it.
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
	// The output pixel's footprint on the frame: CameraAttributesPhysical's sensor height (24 mm unless its sensor
	// format says otherwise, and always for CameraAttributesPractical).
	const double pixel_um = cam_attributes->sensor_size.y * 1000.0 / p_output_height;

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

/* Camera finish
 *
 * In-camera noise reduction and sharpening, which a camera applies to its processed (display-encoded) image before
 * recording it. The camera finish pass runs them after tonemapping, at the output resolution, on Y'CbCr with Rec. 709
 * coefficients (ITU-R BT.709-6, table 3), which is how video cameras process the image.
 *
 * Both are defined relative to the frame, not to pixels, so they look the same at every output resolution: their
 * sizes are given in pixels of a 1080-line frame and scaled by the output height here.
 *
 * Noise reduction is a bilateral filter (Tomasi and Manduchi, "Bilateral Filtering for Gray and Color Images",
 * ICCV 1998): a Gaussian in space times a Gaussian in value, so fluctuations much smaller than the range sigma are
 * averaged away while steps much larger than it are kept. Strength scales both kernels, as a camera's NR levels do:
 * at strength 1 the range sigma is 0.04 of full scale (about 10 8-bit code values, above the noise of the digital
 * grain model at ISO 6400 in 1080p, see SUMMARY of task 37) and the spatial sigma 1.5 pixels of a 1080-line frame; at
 * strength 4 fine texture of up to 0.16 of full scale is smeared over 3 pixels, the waxy look of a phone at high ISO.
 * Those two numbers are a look choice. Chroma is filtered with twice the luma's range sigma, because cameras reduce
 * chroma noise harder than luma noise (the eye's chroma acuity is lower); chroma-only leaves luma untouched.
 *
 * Sharpening is an unsharp mask on luma (Y' + amount * (Y' - G_sigma * Y'), added equally to R'G'B' so no color
 * fringes appear), with the sigma given as the radius in pixels of a 1080-line frame. Halo control limits the result
 * to the range of the 3x3 neighborhood widened by halo times that range, the clamp AMD's contrast adaptive sharpening
 * uses to avoid ringing (FidelityFX CAS, Lottes 2019): 0 allows no overshoot at all, 1 a full neighborhood range.
 */

void RendererCameraAttributes::camera_attributes_set_noise_reduction(RID p_camera_attributes, bool p_enable, float p_strength, bool p_chroma_only) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->noise_reduction_enabled = p_enable;
	cam_attributes->noise_reduction_strength = MAX(p_strength, 0.0f);
	cam_attributes->noise_reduction_chroma_only = p_chroma_only;
}

void RendererCameraAttributes::camera_attributes_set_sharpening(RID p_camera_attributes, bool p_enable, float p_amount, float p_radius, float p_halo) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->sharpening_enabled = p_enable;
	cam_attributes->sharpening_amount = MAX(p_amount, 0.0f);
	cam_attributes->sharpening_radius = MAX(p_radius, 0.01f);
	cam_attributes->sharpening_halo = MAX(p_halo, 0.0f);
}

RendererCameraAttributes::CameraFinishSettings RendererCameraAttributes::camera_attributes_get_finish(RID p_camera_attributes, int p_output_height) {
	CameraFinishSettings settings;
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	if (!cam_attributes || p_output_height <= 0) {
		return settings;
	}
	const float px_per_1080 = p_output_height / 1080.0f;

	if (cam_attributes->noise_reduction_enabled && cam_attributes->noise_reduction_strength > 0.0f) {
		const float strength = cam_attributes->noise_reduction_strength;
		settings.noise_reduction = true;
		settings.nr_range_sigma = 0.04f * strength;
		settings.nr_spatial_sigma_px = (1.0f + 0.5f * strength) * px_per_1080;
		settings.nr_chroma_only = cam_attributes->noise_reduction_chroma_only;
	}

	if (cam_attributes->sharpening_enabled && cam_attributes->sharpening_amount > 0.0f) {
		settings.sharpening = true;
		settings.sharpen_amount = cam_attributes->sharpening_amount;
		settings.sharpen_sigma_px = cam_attributes->sharpening_radius * px_per_1080;
		settings.sharpen_halo = cam_attributes->sharpening_halo;
	}
	return settings;
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

/* Lens optics
 *
 * Everything that happens to the light between the scene and the sensor: geometric distortion, lateral chromatic
 * aberration, diffraction and vignetting. The tonemap pass applies them first, to scene-referred light, before white
 * balance, grain and the tone curve (camera_lens_inc.glsl; the Compatibility renderer's post pass has a copy). The
 * parameters cross in the lens region of the CameraEffects uniform buffer, worked out here per pass because most of
 * them depend on the frame's aspect and resolution.
 *
 * Coordinates: the shader works in frame positions q, centered, scaled so the output frame's corner is at |q| = 1
 * (lens[0].xy is the half extent per axis). With CameraAttributesPhysical the frame is the sensor: its height maps to
 * the output height (Camera3D keeps the vertical field of view), or for an anamorphic lens its desqueezed width maps to
 * the output width. Everything spatial is therefore relative to the frame, so it looks the same at any resolution.
 */

// Relative radial displacement at the frame corner of a typical rectilinear lens, as a function of the tangent of the
// corner's field angle. A look choice fitted to the range of published lens tests of full-frame primes and zooms
// (lenstip.com, DxOMark, opticallimits.com): about -5% (barrel) at 14 mm, -1.7% at 24 mm, -0.7% at 35 mm, near 0
// around 50-60 mm and a slight pincushion (+0.1-0.3%) on short telephotos. Real lenses scatter widely around it.
double RendererCameraAttributes::lens_typical_corner_distortion(double p_tan_corner) {
	return 0.003 - 0.025 * p_tan_corner * p_tan_corner;
}

// Brown-Conrady radial distortion with a separate pair of coefficients per axis (equal for a spherical lens), from the
// undistorted frame position to the distorted one: x_d = x (1 + k1 r^2 + k2 r^4), r measured in frame units.
static void _lens_distort(double p_x, double p_y, const double p_k[4], double &r_x, double &r_y) {
	const double r2 = p_x * p_x + p_y * p_y;
	r_x = p_x * (1.0 + p_k[0] * r2 + p_k[1] * r2 * r2);
	r_y = p_y * (1.0 + p_k[2] * r2 + p_k[3] * r2 * r2);
}

// The largest zoom-in factor's inverse (<= 1) that keeps every output pixel inside the rendered frame: the frame's
// border, distorted, is the edge of the image; the biggest output rectangle of the frame's shape inside it touches
// it where its boundary is closest to the center in the rectangle's own metric.
static double _lens_distortion_crop(double p_half_x, double p_half_y, const double p_k[4]) {
	double crop = 1.0;
	const int steps = 64;
	for (int edge = 0; edge < 4; edge++) {
		for (int i = 0; i <= steps; i++) {
			const double t = -1.0 + 2.0 * i / steps;
			double x = edge < 2 ? t * p_half_x : (edge == 2 ? -p_half_x : p_half_x);
			double y = edge < 2 ? (edge == 0 ? -p_half_y : p_half_y) : t * p_half_y;
			double dx, dy;
			_lens_distort(x, y, p_k, dx, dy);
			crop = MIN(crop, MAX(Math::abs(dx) / p_half_x, Math::abs(dy) / p_half_y));
		}
	}
	return MAX(crop, 0.05);
}

// Diffraction kernel: 5x5 bilinear taps at multiples of a spacing h per axis, weighted by a Gaussian sampled at the
// taps (exp(-k^2 / 2), normalized; camera_lens_inc.glsl has the same constants). A tap at a fractional texel offset
// blends two texels, which adds variance of its own, so h is solved for the kernel's actual variance.
static constexpr double LENS_DIFFRACTION_WEIGHTS[3] = { 0.402619947, 0.244201342, 0.054488685 };

static double _lens_diffraction_variance(double p_spacing) {
	double variance = 0.0;
	for (int k = -2; k <= 2; k++) {
		const double offset = Math::abs(k) * p_spacing;
		const double frac = offset - Math::floor(offset);
		variance += LENS_DIFFRACTION_WEIGHTS[Math::abs(k)] * (offset * offset + frac * (1.0 - frac));
	}
	return variance;
}

static double _lens_diffraction_spacing(double p_sigma_texels) {
	// The variance grows monotonically with the spacing (the bilinear term has a slope of at least 1 - 2 frac).
	double lo = 0.0;
	double hi = 2.0 * p_sigma_texels + 2.0;
	const double target = p_sigma_texels * p_sigma_texels;
	for (int i = 0; i < 48; i++) {
		const double mid = 0.5 * (lo + hi);
		if (_lens_diffraction_variance(mid) < target) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return 0.5 * (lo + hi);
}

void RendererCameraAttributes::camera_attributes_set_lens(RID p_camera_attributes, const Vector2 &p_sensor_size, float p_focal_length, float p_fov_focal_length, float p_focus_distance, float p_f_number, float p_max_f_number, float p_anamorphic_squeeze) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->sensor_size = Vector2(MAX(p_sensor_size.x, 0.1f), MAX(p_sensor_size.y, 0.1f));
	cam_attributes->lens_focal_length = MAX(p_focal_length, 0.0f);
	cam_attributes->lens_fov_focal_length = MAX(p_fov_focal_length, 0.0f);
	cam_attributes->lens_focus_distance = MAX(p_focus_distance, 0.0f);
	cam_attributes->lens_f_number = MAX(p_f_number, 0.1f);
	cam_attributes->lens_max_f_number = MAX(p_max_f_number, 0.1f);
	cam_attributes->lens_anamorphic_squeeze = CLAMP(p_anamorphic_squeeze, 1.0f, 4.0f);
}

void RendererCameraAttributes::camera_attributes_set_vignetting(RID p_camera_attributes, bool p_enable, float p_strength, float p_falloff) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->vignetting_enabled = p_enable;
	cam_attributes->vignetting_strength = MAX(p_strength, 0.0f);
	cam_attributes->vignetting_falloff = MAX(p_falloff, 0.1f);
}

void RendererCameraAttributes::camera_attributes_set_lens_distortion(RID p_camera_attributes, bool p_enable, bool p_automatic, float p_k1, float p_k2, bool p_auto_crop) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->distortion_enabled = p_enable;
	cam_attributes->distortion_automatic = p_automatic;
	// Past these the mapping folds over inside the frame.
	cam_attributes->distortion_k1 = CLAMP(p_k1, -0.5f, 0.5f);
	cam_attributes->distortion_k2 = CLAMP(p_k2, -0.5f, 0.5f);
	cam_attributes->distortion_auto_crop = p_auto_crop;
}

void RendererCameraAttributes::camera_attributes_set_chromatic_aberration(RID p_camera_attributes, bool p_enable, float p_strength) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->chromatic_aberration_enabled = p_enable;
	cam_attributes->chromatic_aberration_strength = CLAMP(p_strength, -100.0f, 100.0f);
}

void RendererCameraAttributes::camera_attributes_set_diffraction(RID p_camera_attributes, bool p_enable, float p_strength) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->diffraction_enabled = p_enable;
	cam_attributes->diffraction_strength = MAX(p_strength, 0.0f);
}

// The lens effects the attributes ask for, before the per-pass work (which can still drop diffraction too small to
// matter). Anything here makes the renderers use the CameraEffects buffer.
uint32_t RendererCameraAttributes::_lens_requested_bits(const CameraAttributes &p_attributes) {
	const bool physical = p_attributes.lens_focal_length > 0.0f;
	uint32_t bits = 0;
	if (p_attributes.distortion_enabled && ((p_attributes.distortion_automatic && physical) || p_attributes.distortion_k1 != 0.0f || p_attributes.distortion_k2 != 0.0f)) {
		bits |= LENS_DISTORTION;
	}
	if (p_attributes.chromatic_aberration_enabled && p_attributes.chromatic_aberration_strength != 0.0f) {
		bits |= LENS_CHROMATIC_ABERRATION;
	}
	if (p_attributes.diffraction_enabled && physical && p_attributes.diffraction_strength > 0.0f) {
		bits |= LENS_DIFFRACTION;
	}
	if (p_attributes.vignetting_enabled && p_attributes.vignetting_strength > 0.0f) {
		bits |= physical ? LENS_VIGNETTING_PHYSICAL : LENS_VIGNETTING_PRACTICAL;
	}
	return bits;
}

void RendererCameraAttributes::_fill_effects_lens(const CameraAttributes &p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo) {
	uint32_t bits = _lens_requested_bits(p_attributes);
	const bool physical = p_attributes.lens_focal_length > 0.0f;
	if (bits == 0 && !physical) {
		return;
	}
	const Size2i size = p_context.source_size.x > 0 && p_context.source_size.y > 0 ? p_context.source_size : p_context.output_size;
	if (size.x <= 0 || size.y <= 0) {
		return;
	}

	// The frame on the sensor, in millimeters, desqueezed horizontally. Without a physical lens only its shape matters.
	const double aspect = double(size.x) / double(size.y);
	const double sensor_w = p_attributes.sensor_size.x;
	const double sensor_h = p_attributes.sensor_size.y;
	const double squeeze = physical ? p_attributes.lens_anamorphic_squeeze : 1.0;
	double frame_w, frame_h;
	if (squeeze != 1.0) {
		frame_w = squeeze * sensor_w; // Camera3D keeps the horizontal field of view for an anamorphic lens.
		frame_h = frame_w / aspect;
	} else {
		frame_h = sensor_h;
		frame_w = aspect * frame_h;
	}
	const double frame_radius = 0.5 * Math::sqrt(frame_w * frame_w + frame_h * frame_h);
	const double half_x = 0.5 * frame_w / frame_radius;
	const double half_y = 0.5 * frame_h / frame_radius;
	// The sensor's own half diagonal in the same desqueezed millimeters.
	const double sensor_radius = 0.5 * Math::sqrt(squeeze * sensor_w * squeeze * sensor_w + sensor_h * sensor_h);
	const double focal = p_attributes.lens_fov_focal_length > 0.0f ? p_attributes.lens_fov_focal_length : p_attributes.lens_focal_length;

	// Distortion coefficients per axis, in frame units.
	double k[4] = { 0.0, 0.0, 0.0, 0.0 };
	if (bits & LENS_DISTORTION) {
		if (p_attributes.distortion_automatic && physical) {
			// The typical lens's corner distortion as a k1 at the sensor's corner, rescaled to the frame's. An
			// anamorphic lens covers its wider horizontal field like a spherical lens of focal length f / squeeze, so
			// the horizontal axis takes that lens's (stronger) distortion.
			const double scale = (frame_radius / sensor_radius) * (frame_radius / sensor_radius);
			const double tan_corner = sensor_radius / focal;
			k[0] = lens_typical_corner_distortion(tan_corner * squeeze) * scale;
			k[2] = lens_typical_corner_distortion(tan_corner) * scale;
		} else {
			k[0] = k[2] = p_attributes.distortion_k1;
			k[1] = k[3] = p_attributes.distortion_k2;
		}
	}
	const double crop = (bits & LENS_DISTORTION) && p_attributes.distortion_auto_crop ? _lens_distortion_crop(half_x, half_y, k) : 1.0;

	r_ubo.lens[0][0] = half_x;
	r_ubo.lens[0][1] = half_y;
	r_ubo.lens[0][2] = crop;
	r_ubo.lens[0][3] = squeeze;
	r_ubo.lens[1][0] = k[0];
	r_ubo.lens[1][1] = k[1];
	r_ubo.lens[1][2] = k[2];
	r_ubo.lens[1][3] = k[3];

	if (bits & LENS_CHROMATIC_ABERRATION) {
		// Lateral color: each channel's image is magnified slightly differently, so fringes grow linearly toward the
		// edges. Strength 1 spreads red and blue by 0.1% of the distance from the center, Imatest's boundary between
		// "minor" and "moderate" lateral CA (imatest.com, "Chromatic aberration"); red is the larger image, as in an
		// undercorrected lens. Lateral color comes from the chief ray, so unlike longitudinal color it does not change
		// with the aperture (Smith, Modern Optical Engineering, 4th ed., ch. 5).
		const double delta = 0.0005 * p_attributes.chromatic_aberration_strength;
		r_ubo.lens[2][0] = 1.0 / (1.0 + delta); // Red: sampled closer to the center, so it lands farther out.
		r_ubo.lens[2][1] = 1.0 / (1.0 - delta); // Blue.
	}

	if (bits & LENS_VIGNETTING_PHYSICAL) {
		// Natural vignetting, the cos^4 law: illuminance falls with cos^4 of the field angle (Kingslake, Lenses in
		// Photography; Ray, Applied Photographic Optics, 3rd ed., section 6.3). tan^2 of the angle at frame
		// position q is |q|^2 (frame radius / focal length)^2, with the focal length that sets the field of view.
		// Optical (mechanical) vignetting: the rear of the lens barrel clips the aperture's image for off-axis
		// points, so only the overlap of two circles passes ("cat's eye"). The pupil is modeled at the barrel's size
		// wide open and offset by half its radius at the sensor's corner, so the corner loses about 0.55 stop wide
		// open and nothing from two stops down, the range lens tests report for fast lenses (Ray, section 6.3: optical
		// vignetting "is usually eliminated by stopping down two or three stops").
		const double max_f = MIN(p_attributes.lens_max_f_number, p_attributes.lens_f_number);
		r_ubo.lens[3][0] = p_attributes.vignetting_strength;
		r_ubo.lens[3][1] = (frame_radius / focal) * (frame_radius / focal);
		r_ubo.lens[3][2] = 0.5 * frame_radius / sensor_radius;
		r_ubo.lens[3][3] = max_f / p_attributes.lens_f_number;
	}
	if (bits & LENS_VIGNETTING_PRACTICAL) {
		// Without a physical lens: strength stops of darkening at the frame's corner, growing as |q|^falloff.
		r_ubo.lens[4][0] = p_attributes.vignetting_strength;
		r_ubo.lens[4][1] = p_attributes.vignetting_falloff;
	}

	if (bits & LENS_DIFFRACTION) {
		// The Airy disk of a circular aperture has its first dark ring at 1.22 lambda N (diameter 2.44 lambda N; Born
		// & Wolf, Principles of Optics, 8.5.2); its best-fitting Gaussian has sigma = 0.42 lambda N (Zhang, Zerubia
		// and Olivo-Marin, Applied Optics 46(10), 2007). N is the working f-number, N (1 + m) at magnification m
		// (thin lens, pupil magnification 1), so it grows at close focus. lambda = 550 nm, the peak of photopic
		// vision. The kernel is in source texels: an output pixel covers frame height / height of the sensor, so the
		// blur stays the same fraction of the frame at any resolution. An anamorphic lens stretches it horizontally.
		const double u = MAX(p_attributes.lens_focus_distance * 1000.0, p_attributes.lens_focal_length + 1.0);
		const double magnification = p_attributes.lens_focal_length / (u - p_attributes.lens_focal_length);
		const double working_f = p_attributes.lens_f_number * (1.0 + magnification);
		const double sigma_mm = 0.42 * 550e-6 * working_f * p_attributes.diffraction_strength;
		const double texel_mm = frame_h / size.y;
		const double sigma_x = sigma_mm * squeeze / texel_mm;
		const double sigma_y = sigma_mm / texel_mm;
		if (MAX(sigma_x, sigma_y) < 0.05) {
			bits &= ~LENS_DIFFRACTION; // Far below a texel: nothing to see, so skip the 25 taps.
		} else {
			r_ubo.lens[5][0] = _lens_diffraction_spacing(sigma_x) / size.x;
			r_ubo.lens[5][1] = _lens_diffraction_spacing(sigma_y) / size.y;
			r_ubo.lens[5][2] = sigma_x;
			r_ubo.lens[5][3] = sigma_y;
		}
	}

	// The physical lens for effects that shape light by it (task 35's oval bokeh and starbursts read the squeeze in
	// lens[0].w and these).
	if (physical) {
		r_ubo.lens[6][0] = sensor_w;
		r_ubo.lens[6][1] = sensor_h;
		r_ubo.lens[6][2] = focal;
		r_ubo.lens[6][3] = p_attributes.lens_f_number;
	}
	r_ubo.lens[7][0] = 1.0 / size.x;
	r_ubo.lens[7][1] = 1.0 / size.y;

	r_ubo.header[0] = bits;
}

/* Aperture, bokeh, tilt-shift, starburst and halation */

void RendererCameraAttributes::camera_attributes_set_aperture(RID p_camera_attributes, bool p_enable, int p_blade_count, float p_blade_curvature, float p_rotation) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->aperture_enabled = p_enable;
	// 0 is a round opening; fewer than 3 blades cannot close a polygon.
	cam_attributes->aperture_blade_count = p_blade_count < 3 ? 0 : MIN(p_blade_count, 32);
	cam_attributes->aperture_blade_curvature = CLAMP(p_blade_curvature, 0.0f, 1.0f);
	cam_attributes->aperture_rotation = p_rotation;
}

void RendererCameraAttributes::camera_attributes_set_cats_eye(RID p_camera_attributes, bool p_enable, float p_strength) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->cats_eye_enabled = p_enable;
	// Past 2 the barrel's circle can leave the aperture's center, and the bokeh is no longer star-shaped around it.
	cam_attributes->cats_eye_strength = CLAMP(p_strength, 0.0f, 2.0f);
}

void RendererCameraAttributes::camera_attributes_set_tilt_shift(RID p_camera_attributes, bool p_enable, float p_tilt, float p_axis) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->tilt_shift_enabled = p_enable;
	cam_attributes->tilt_shift_tilt = CLAMP(p_tilt, -45.0f, 45.0f);
	cam_attributes->tilt_shift_axis = p_axis;
}

void RendererCameraAttributes::camera_attributes_set_starburst(RID p_camera_attributes, bool p_enable, float p_strength) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->starburst_enabled = p_enable;
	cam_attributes->starburst_strength = MAX(p_strength, 0.0f);
}

void RendererCameraAttributes::camera_attributes_set_halation(RID p_camera_attributes, bool p_enable, float p_strength, float p_radius) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	ERR_FAIL_NULL(cam_attributes);
	cam_attributes->halation_enabled = p_enable;
	cam_attributes->halation_strength = CLAMP(p_strength, 0.0f, 12.0f);
	cam_attributes->halation_radius = MAX(p_radius, 0.01f);
}

// The frame on the sensor in millimeters, desqueezed horizontally, for an image p_aspect wide per unit of height (as in
// _fill_effects_lens()). Without a physical lens: a 24 mm tall frame, the full-frame height.
static void _camera_frame_mm(bool p_physical, const Vector2 &p_sensor_size, double p_squeeze, double p_aspect, double &r_frame_w, double &r_frame_h) {
	if (!p_physical) {
		r_frame_h = 24.0;
		r_frame_w = p_aspect * r_frame_h;
	} else if (p_squeeze != 1.0) {
		r_frame_w = p_squeeze * p_sensor_size.x;
		r_frame_h = r_frame_w / p_aspect;
	} else {
		r_frame_h = p_sensor_size.y;
		r_frame_w = p_aspect * r_frame_h;
	}
}

bool RendererCameraAttributes::camera_attributes_uses_tilt_shift(RID p_camera_attributes) {
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	return cam_attributes && cam_attributes->tilt_shift_enabled && cam_attributes->tilt_shift_tilt != 0.0f && cam_attributes->lens_focal_length > 0.0f;
}

// The bokeh is the image of the aperture, so the DOF draws it with the aperture's shape:
// - Blades: a regular polygon with blade_count corners, its edges bowed out toward the round opening by the blade
//   curvature (the shader mixes the polygon's radius with the circle's).
// - Anamorphic squeeze: an anamorphic lens compresses the scene horizontally and the image is stretched back by the
//   squeeze, but the aperture is not squeezed; defocused points come out as ovals taller than wide by the squeeze.
// - Cat's eye: off axis the lens barrel clips the aperture, so the bokeh is the overlap of two circles (the optical
//   vignetting of _fill_effects_lens(): pupil radius max f / N of the barrel's, offset by half its radius at the
//   sensor's corner wide open; Ray, Applied Photographic Optics, 3rd ed., section 6.3). Relative to the aperture the
//   barrel's circle has radius N / max f and is offset by 0.5 N / max f per sensor-corner unit: an elongated bokeh at
//   the corners wide open, round again two stops down. Without a physical lens: a lens wide open.
// - Near and far bokeh are point reflections of each other (the light cone of a point in front of the focus plane has
//   not crossed yet when it reaches the sensor), which matters for odd blade counts.
RendererCameraAttributes::BokehShape RendererCameraAttributes::camera_attributes_get_bokeh_shape(RID p_camera_attributes, float p_aspect) {
	BokehShape shape;
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	if (!cam_attributes) {
		return shape;
	}
	const bool physical = cam_attributes->lens_focal_length > 0.0f;
	const double squeeze = physical ? cam_attributes->lens_anamorphic_squeeze : 1.0;
	const bool cats_eye = cam_attributes->cats_eye_enabled && cam_attributes->cats_eye_strength > 0.0f;
	shape.tilt = camera_attributes_uses_tilt_shift(p_camera_attributes);
	shape.custom = cam_attributes->aperture_enabled || cats_eye || squeeze != 1.0;
	if (!shape.custom && !shape.tilt) {
		return shape;
	}

	if (cam_attributes->aperture_enabled) {
		shape.blade_count = cam_attributes->aperture_blade_count;
		shape.blade_curvature = cam_attributes->aperture_blade_curvature;
		shape.rotation = Math::deg_to_rad(cam_attributes->aperture_rotation);
	} else {
		// The project's shape, drawn the same way: the box is a square standing on its side, the hexagon a hexagon.
		switch (dof_blur_bokeh_shape) {
			case RSE::DOF_BOKEH_BOX:
				shape.blade_count = 4;
				shape.rotation = Math::PI / 4.0;
				break;
			case RSE::DOF_BOKEH_HEXAGON:
				shape.blade_count = 6;
				break;
			default:
				shape.blade_count = 0;
				break;
		}
	}
	shape.squeeze = squeeze;

	const double aspect = MAX(double(p_aspect), 0.01);
	double frame_w, frame_h;
	_camera_frame_mm(physical, cam_attributes->sensor_size, squeeze, aspect, frame_w, frame_h);
	const double frame_radius = 0.5 * Math::sqrt(frame_w * frame_w + frame_h * frame_h);
	shape.frame_half_extent = Vector2(0.5 * frame_w / frame_radius, 0.5 * frame_h / frame_radius);
	if (cats_eye) {
		double stops_ratio = 1.0; // N / max f.
		double offset_per_unit = 0.5; // Pupil offset at |q| = 1, in barrel radii.
		if (physical) {
			const double sensor_w = cam_attributes->sensor_size.x;
			const double sensor_h = cam_attributes->sensor_size.y;
			const double sensor_radius = 0.5 * Math::sqrt(squeeze * sensor_w * squeeze * sensor_w + sensor_h * sensor_h);
			stops_ratio = cam_attributes->lens_f_number / MIN(cam_attributes->lens_max_f_number, cam_attributes->lens_f_number);
			offset_per_unit = 0.5 * frame_radius / sensor_radius;
		}
		shape.cats_eye_radius = stops_ratio;
		shape.cats_eye_offset = cam_attributes->cats_eye_strength * offset_per_unit * stops_ratio;
	}

	if (shape.tilt) {
		// Scheimpflug: a lens tilted by theta about an axis focuses on a plane through the hinge line, which lies at
		// J = f / sin(theta) from the lens on the side the lens tilts toward (Merklinger, "Focusing the View Camera",
		// 1993, the hinge rule), and through the point the lens focuses on along its axis. With the tilt axis a and b
		// its perpendicular in the frame (both in view space, the camera looking down -Z), the plane holds a and the
		// line from the hinge (-J b) to (0, 0, -s), so its normal is a x (J b - s z) = J z + s b, i.e. proportional to
		// s sin(theta) b + f z. Positive tilt brings the bottom of the frame closer (the ground-plane focus of a
		// landscape lens); negative tilt is the "reverse tilt" that fakes a miniature.
		const double theta = Math::deg_to_rad(double(cam_attributes->tilt_shift_tilt));
		const double axis = Math::deg_to_rad(double(cam_attributes->tilt_shift_axis));
		const double f = cam_attributes->lens_focal_length / 1000.0;
		const double s = MAX(double(cam_attributes->lens_focus_distance), f + 0.001);
		const Vector3 b = Vector3(-Math::sin(axis), Math::cos(axis), 0.0);
		shape.focus_plane_normal = (b * (s * Math::sin(theta)) + Vector3(0.0, 0.0, f)).normalized();
		shape.focal_length = f;
	}
	return shape;
}

// The glare effects the attributes and the Environment ask for; the camera's need the attributes, the Environment's
// run without them.
uint32_t RendererCameraAttributes::_glare_requested_bits(const CameraAttributes *p_attributes, const CameraGlareEnvironment &p_environment) {
	uint32_t bits = 0;
	if (p_environment.lens_flare_enabled && (p_environment.lens_flare_intensity > 0.0f || p_environment.lens_flare_halo_intensity > 0.0f) && p_environment.lens_flare_ghost_count > 0) {
		bits |= GLARE_LENS_FLARE;
	}
	if (p_environment.veiling_glare_enabled && p_environment.veiling_glare_strength > 0.0f) {
		bits |= GLARE_VEILING;
	}
	if (p_environment.diffusion_filter_enabled && p_environment.diffusion_filter_strength > 0.0f) {
		bits |= GLARE_DIFFUSION | (p_environment.diffusion_filter_black ? GLARE_DIFFUSION_BLACK : 0);
	}
	if (p_attributes) {
		if (p_attributes->starburst_enabled && p_attributes->starburst_strength > 0.0f && p_attributes->aperture_blade_count >= 3 && p_attributes->aperture_blade_curvature < 1.0f) {
			bits |= GLARE_STARBURST;
		}
		if (p_attributes->halation_enabled && p_attributes->halation_strength > 0.0f) {
			bits |= GLARE_HALATION;
		}
	}
	return bits;
}

bool RendererCameraAttributes::camera_attributes_uses_glare(RID p_camera_attributes, const CameraGlareEnvironment &p_environment) {
	return _glare_requested_bits(camera_attributes_owner.get_or_null(p_camera_attributes), p_environment) != 0;
}

// The glare chain's lod 0 is a Gaussian blur of the half-resolution scene; each lod halves the resolution and blurs
// again. Its blur, measured on a single bright texel (tools/measure_chain.gd in the task's results), is sigma =
// GLARE_CHAIN_SIGMA * 2^lod texels of lod 0 for lod >= 1; lod 0 is sharper. Effects ask for a blur in frame heights and
// get the lod that gives it.
static constexpr double GLARE_CHAIN_SIGMA = 1.0;

static double _glare_lod_for_sigma(double p_sigma_frame, double p_chain_height, double p_max_lod) {
	const double sigma_texels = p_sigma_frame * p_chain_height;
	return CLAMP(Math::log2(MAX(sigma_texels / GLARE_CHAIN_SIGMA, 1e-6)), 0.0, p_max_lod);
}

// Glare: light that leaves the path an ideal lens would give it. Every effect is a point spread function convolved
// with the scene-linear image (the glare chain, a mip chain of the whole frame, unthresholded), so dim light makes dim
// glare and only bright sources show it, as with a real lens. The tonemap pass adds it before vignetting (it happens
// in the lens or a filter in front of it), except halation (in the film, after the lens). Sizes are in frame heights,
// so they look the same at every resolution. Where a value is a look choice, the comment says so.
void RendererCameraAttributes::_fill_effects_glare(const CameraAttributes *p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo) {
	uint32_t bits = _glare_requested_bits(p_attributes, p_context.glare_environment);
	const Size2i size = p_context.source_size.x > 0 && p_context.source_size.y > 0 ? p_context.source_size : p_context.output_size;
	if (bits == 0 || p_context.glare_chain_size.x <= 0 || p_context.glare_chain_size.y <= 0 || p_context.glare_chain_mipmaps <= 0 || size.x <= 0 || size.y <= 0) {
		return;
	}
	const CameraGlareEnvironment &env = p_context.glare_environment;
	const double aspect = double(size.x) / double(size.y);
	const double chain_h = p_context.glare_chain_size.y;
	const double max_lod = p_context.glare_chain_mipmaps - 1;
	const bool physical = p_attributes && p_attributes->lens_focal_length > 0.0f;
	const double squeeze = physical ? p_attributes->lens_anamorphic_squeeze : 1.0;

	r_ubo.glare[1][1] = aspect;
	r_ubo.glare[1][2] = 1.0 / p_context.glare_chain_size.x;
	r_ubo.glare[1][3] = 1.0 / p_context.glare_chain_size.y;
	r_ubo.glare[2][2] = max_lod;
	r_ubo.glare[5][0] = chain_h / GLARE_CHAIN_SIGMA; // lod = log2(sigma in frame heights * this).

	if (bits & GLARE_LENS_FLARE) {
		// Ghosts: light reflected twice between lens surfaces lands mirrored through the frame's center, scaled and
		// defocused (the screen-space method of Kawase, "Practical Implementation of High Dynamic Range Rendering",
		// GDC 2004, and Chapman, "Pseudo Lens Flare", 2013). Each ghost carries the product of two surface
		// reflectances: about 4% squared (0.16%) for uncoated glass and 1.3% squared (0.017%) for a single MgF2 layer
		// (Fresnel at n = 1.5 and 1.38; Kingslake, Lens Design Fundamentals, ch. 2). Intensity 1 is 0.1% per ghost,
		// between the two: a look choice for an older lens, since modern multicoating would hide the flare of anything
		// short of the sun. The halo (a ring centered on the frame, from the reflection off a curved front element) is
		// the same order. Only light above the threshold, in the exposed image where 1 is display white, makes flare:
		// a convenience, as the dim parts of the frame would add only a faint veil.
		r_ubo.glare[0][0] = 0.001 * env.lens_flare_intensity;
		r_ubo.glare[0][1] = MAX(env.lens_flare_threshold, 0.0f);
		r_ubo.glare[0][2] = CLAMP(env.lens_flare_ghost_count, 1, 8);
		r_ubo.glare[0][3] = 0.001 * env.lens_flare_halo_intensity;
		// Coatings and glass dispersion give each ghost a slightly different size per color; 2% red/blue at 1 (look).
		r_ubo.glare[1][0] = 0.02 * env.lens_flare_chromatic_aberration;
	}
	if (bits & GLARE_VEILING) {
		// Veiling glare: light scattered by the glass, coatings and barrel spread over the whole frame, measured by the
		// veiling glare index (ISO 9358:1994); 1-3% is typical of modern camera lenses (Ray, Applied Photographic
		// Optics, section 16.4). Strength 1 is 2% of the frame's mean light, added everywhere, taken from the image.
		r_ubo.glare[2][0] = MIN(0.02 * env.veiling_glare_strength, 0.5);
	}
	if (bits & GLARE_DIFFUSION) {
		// Diffusion filter (Tiffen Pro-Mist and Black Pro-Mist): particles in the glass scatter a fraction of all the
		// light into a wide soft halo, which spreads highlights and lifts the shadows near them. The point spread
		// function is (1 - s) delta + s G, energy-preserving; strength is the filter's grade (1/8, 1/4, 1/2, 1, 2) and
		// s = 0.15 per grade, with G a sum of blurs from 1% to 8% of the frame height. The black variant scatters
		// half as much into a tighter halo (0.5% to 2%): its black particles absorb the wide scatter, which is what
		// keeps its shadows dark. Both are look choices from the makers' sample frames, not measurements.
		const bool black = bits & GLARE_DIFFUSION_BLACK;
		r_ubo.glare[2][1] = MIN((black ? 0.075 : 0.15) * env.diffusion_filter_strength, 0.75);
		r_ubo.glare[2][3] = _glare_lod_for_sigma(black ? 0.005 : 0.01, chain_h, max_lod);
	}

	// The lens the camera's effects derive from. Without one: a 50 mm lens at f/8 on a 24 mm tall frame (look choice).
	double frame_w, frame_h;
	_camera_frame_mm(physical, p_attributes ? p_attributes->sensor_size : Vector2(36.0, 24.0), squeeze, aspect, frame_w, frame_h);
	const double focal = physical ? (p_attributes->lens_fov_focal_length > 0.0f ? p_attributes->lens_fov_focal_length : p_attributes->lens_focal_length) : 50.0;
	const double f_number = physical ? p_attributes->lens_f_number : 8.0;

	if (bits & GLARE_STARBURST) {
		// Diffraction spikes: each straight edge of the aperture diffracts light perpendicular to itself. For an
		// aperture of area A with an edge of length l, the far field along the edge's normal is l / (2 pi nu) in
		// amplitude at spatial frequency nu, and integrating the point spread function across the spike gives a line
		// density of lambda f l / (4 pi^2 A x^2) of the source's light per unit length at distance x on the sensor (Born
		// & Wolf, Principles of Optics, 8.3 and 8.5; Goodman, Introduction to Fourier Optics, 4.2). A smaller aperture
		// (larger N) makes it stronger, as photographers know: lambda f l / A grows with N. Opposite edges of an even
		// blade count share a spike (twice the light, N spikes); an odd count gives each edge its own (2 N spikes).
		// lambda = 550 nm. The aperture is a regular polygon with its corners on the circle of diameter f / N; curved
		// blades shorten the straight part and weaken the spikes (1 - curvature, look choice). The spikes start two
		// glare chain texels out (the core is the image itself) and are cut at half the frame height.
		const int blades = p_attributes->aperture_blade_count;
		const double radius = 0.5 * focal / f_number;
		const double edge = 2.0 * radius * Math::sin(Math::PI / blades);
		const double area = 0.5 * blades * radius * radius * Math::sin(2.0 * Math::PI / blades);
		const double edges_per_spike = (blades % 2 == 0) ? 2.0 : 1.0;
		const double density_mm = 550e-6 * focal * edge / (4.0 * Math::PI * Math::PI * area) * edges_per_spike;
		r_ubo.glare[3][0] = density_mm / frame_h * p_attributes->starburst_strength * (1.0 - p_attributes->aperture_blade_curvature);
		r_ubo.glare[3][1] = 2.0 / chain_h;
		// The edges' normals: the corners sit at rotation + k 2 pi / N, so the edges' normals at rotation + (k + 1/2)
		// 2 pi / N; with an even count every other one is the same line.
		r_ubo.glare[3][2] = Math::deg_to_rad(double(p_attributes->aperture_rotation)) + Math::PI / blades;
		r_ubo.glare[3][3] = 2.0 * Math::PI / blades;
		r_ubo.glare[4][0] = (blades % 2 == 0) ? blades / 2 : blades;
		r_ubo.glare[4][1] = squeeze;
		r_ubo.glare[5][1] = 0.5;
	}
	if (bits & GLARE_HALATION) {
		// Halation: light that passes through the emulsion reflects off the back of the film base and exposes the
		// emulsion again around the highlight. Light scattered past the critical angle (41.8 degrees at n = 1.5) is
		// totally reflected, so the halo reaches 2 t tan(41.8) = 0.22 mm for a 0.125 mm base (Kodak, "The Book of Film
		// Care"; Hunt, The Reproduction of Colour, 6th ed., 14.6); modeled as a Gaussian of half that radius, so a
		// larger format has a tighter halo relative to its frame. The red-sensitive layer lies next to the base and the
		// yellow filter layer keeps blue from reaching it, so the halo is red with some green (the shader's weights).
		// Strength 1 sends 8% of the light into the halo: Fresnel's 4% at the base's back surface plus the totally
		// reflected part (look choice; film with its anti-halation backing removed). The light is moved, not added, so
		// the frame's overall color does not change.
		const double sigma_mm = 0.112 * p_attributes->halation_radius;
		r_ubo.glare[4][2] = MIN(0.08 * p_attributes->halation_strength, 0.95);
		r_ubo.glare[4][3] = _glare_lod_for_sigma(sigma_mm / frame_h, chain_h, max_lod);
	}

	r_ubo.header[1] = bits;
}

// Exposure automation (task 36) fills exposure[] and header[2] here.
void RendererCameraAttributes::_fill_effects_exposure(const CameraAttributes &p_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo) {
}

bool RendererCameraAttributes::camera_attributes_fill_effects_buffer(RID p_camera_attributes, const CameraEffectsContext &p_context, CameraEffectsUBO &r_ubo) {
	r_ubo = CameraEffectsUBO();
	CameraAttributes *cam_attributes = camera_attributes_owner.get_or_null(p_camera_attributes);
	// The Environment's glare runs without camera attributes.
	_fill_effects_glare(cam_attributes, p_context, r_ubo);
	if (!cam_attributes) {
		return r_ubo.header[1] != 0;
	}
	_fill_effects_lens(*cam_attributes, p_context, r_ubo);
	_fill_effects_exposure(*cam_attributes, p_context, r_ubo);
	if ((r_ubo.header[0] | r_ubo.header[1] | r_ubo.header[2] | r_ubo.header[3]) == 0) {
		r_ubo = CameraEffectsUBO(); // The physical lens fills some regions regardless; nothing reads them.
		return false;
	}
	return true;
}
