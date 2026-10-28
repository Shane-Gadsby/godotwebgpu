// CameraAttributes LUT for the Compatibility renderer, applied by the post pass (effects/post.glsl). A copy of
// servers/rendering/renderer_rd/shaders/effects/camera_lut_inc.glsl, which documents it: keep the two in step. The packed
// parameters come from the Tonemap UBO, the LUT is bound as `sampler3D camera_lut`.
//
// Two kinds of LUT, picked by the input space:
// - Display: the LUT takes the tonemapped image, sRGB-encoded, and returns it sRGB-encoded (a creative or "look"
//   LUT made for Rec.709/sRGB). Applied after tonemapping and glow.
// - A camera log encoding: the LUT takes scene-linear light converted to that camera's gamut and log curve, as the
//   camera would record it, and returns a display image (a technical "log to Rec.709" LUT, or a look built on one).
//   The LUT then is the tone curve, in place of the Environment's tonemapper.
//
// The parameters arrive packed into one uint (RendererCameraAttributes::camera_attributes_get_lut_packed()), 0 when
// off: bit 0 enabled, bits 1-3 the input space (RSE::CameraAttributesLUTInput), bits 8-15 the strength (0-255).

#define CAMERA_LUT_INPUT_DISPLAY 0u
#define CAMERA_LUT_INPUT_ARRI_LOGC3 1u
#define CAMERA_LUT_INPUT_SONY_SLOG3 2u
#define CAMERA_LUT_INPUT_PANASONIC_VLOG 3u
#define CAMERA_LUT_INPUT_CANON_LOG3 4u
#define CAMERA_LUT_INPUT_RED_LOG3G10 5u
#define CAMERA_LUT_INPUT_ACESCCT 6u

// Samples the LUT at grid coordinates: an input of 0 or 1 lands on the first or last entry's texel center, as
// .cube files and grading tools define it.
vec3 camera_lut_sample(vec3 p_coord) {
	vec3 size = vec3(textureSize(camera_lut, 0));
	vec3 coord = clamp(p_coord, vec3(0.0), vec3(1.0)) * ((size - 1.0) / size) + 0.5 / size;
	return textureLod(camera_lut, coord, 0.0).rgb;
}

vec3 camera_lut_log10(vec3 p_x) {
	return log2(p_x) * 0.30102999566;
}

// Linear Rec.709 -> the camera's gamut and log encoding, as code values over 0-1 (full range, 18% gray at the
// manufacturer's value). Gamut matrices from colour-science 0.4.6 (Bradford adaptation to ACES white for ACEScct);
// curves checked against its reference encodings.
vec3 camera_lut_log_encode(vec3 p_color, uint p_input) {
	vec3 x;
	vec3 y;
	if (p_input == CAMERA_LUT_INPUT_ARRI_LOGC3) {
		// ARRI LogC3 (EI 800), ARRI Wide Gamut 3.
		x = mat3(
					vec3(0.631321175, 0.036819797, 0.017369456),
					vec3(0.270800766, 0.793036840, 0.148788698),
					vec3(0.097878125, 0.170142765, 0.833840518)) *
				p_color;
		y = mix(5.367655 * x + 0.092809, 0.247190 * camera_lut_log10(max(5.555556 * x + 0.052272, vec3(1e-10))) + 0.385537, greaterThan(x, vec3(0.010591)));
	} else if (p_input == CAMERA_LUT_INPUT_SONY_SLOG3) {
		// Sony S-Log3, S-Gamut3.Cine.
		x = mat3(
					vec3(0.645679478, 0.087529992, 0.036957420),
					vec3(0.259114547, 0.759699563, 0.129280905),
					vec3(0.095205975, 0.152770446, 0.833761675)) *
				p_color;
		y = mix((x * ((171.2102946929 - 95.0) / 0.01125) + 95.0) / 1023.0, (420.0 + camera_lut_log10(max((x + 0.01) / 0.19, vec3(1e-10))) * 261.5) / 1023.0, greaterThanEqual(x, vec3(0.01125)));
	} else if (p_input == CAMERA_LUT_INPUT_PANASONIC_VLOG) {
		// Panasonic V-Log, V-Gamut.
		x = mat3(
					vec3(0.585196340, 0.078588445, 0.022794019),
					vec3(0.322642032, 0.819626812, 0.114216729),
					vec3(0.092162166, 0.101784583, 0.862988309)) *
				p_color;
		y = mix(5.6 * x + 0.125, 0.241514 * camera_lut_log10(max(x + 0.00873, vec3(1e-10))) + 0.598206, greaterThanEqual(x, vec3(0.01)));
	} else if (p_input == CAMERA_LUT_INPUT_CANON_LOG3) {
		// Canon Log 3 (v1.2), Cinema Gamut. The curve takes reflectance over 90% white.
		x = mat3(
					vec3(0.556157483, 0.082413253, 0.033104472),
					vec3(0.329174608, 0.757469786, 0.225811960),
					vec3(0.114667909, 0.160116960, 0.741083568)) *
				p_color * (1.0 / 0.9);
		y = 1.9754798 * x + 0.12512219;
		y = mix(y, 0.36726845 * camera_lut_log10(max(x * 14.98325 + 1.0, vec3(1e-10))) + 0.12240537, greaterThan(x, vec3(0.014)));
		y = mix(y, -0.36726845 * camera_lut_log10(max(-x * 14.98325 + 1.0, vec3(1e-10))) + 0.12783901, lessThan(x, vec3(-0.014)));
	} else if (p_input == CAMERA_LUT_INPUT_RED_LOG3G10) {
		// RED Log3G10, REDWideGamutRGB.
		x = mat3(
					vec3(0.541946423, 0.076989315, 0.058872377),
					vec3(0.360148846, 0.767968151, 0.273494548),
					vec3(0.097906078, 0.155042044, 0.667632869)) *
						p_color +
				0.01;
		y = mix(x * 15.1927, 0.224282 * camera_lut_log10(max(x * 155.975327 + 1.0, vec3(1e-10))), greaterThanEqual(x, vec3(0.0)));
	} else {
		// ACEScct, ACES AP1.
		x = mat3(
					vec3(0.613097402, 0.070193722, 0.020615593),
					vec3(0.339523146, 0.916353879, 0.109569773),
					vec3(0.047379451, 0.013452398, 0.869814634)) *
				p_color;
		y = mix(10.5402377416545 * x + 0.0729055341958355, (log2(max(x, vec3(1e-10))) + 9.72) / 17.52, greaterThan(x, vec3(0.0078125)));
	}
	return y;
}

float camera_lut_strength(uint p_packed) {
	return float((p_packed >> 8u) & 0xFFu) * (1.0 / 255.0);
}

// Use in place of apply_tonemapping() for scene light: with a log-input LUT, the LUT is the tone curve (blended with
// the tonemapper by the strength); otherwise this is the tonemapper.
vec3 apply_camera_tonemapping(vec3 p_color, uint p_packed) {
	vec3 tonemapped = apply_tonemapping(p_color);
	uint input_space = (p_packed >> 1u) & 7u;
	if ((p_packed & 1u) == 0u || input_space == CAMERA_LUT_INPUT_DISPLAY) {
		return tonemapped;
	}
	vec3 graded = camera_lut_sample(camera_lut_log_encode(p_color, input_space));
	return srgb_to_linear(max(mix(linear_to_srgb(clamp(tonemapped, vec3(0.0), vec3(1.0))), graded, camera_lut_strength(p_packed)), vec3(0.0)));
}

// A display LUT, on the tonemapped (linear) image. The LUT's domain is SDR: brighter output is clipped to 1.0.
vec3 apply_camera_display_lut(vec3 p_color, uint p_packed) {
	if ((p_packed & 1u) == 0u || ((p_packed >> 1u) & 7u) != CAMERA_LUT_INPUT_DISPLAY) {
		return p_color;
	}
	vec3 encoded = linear_to_srgb(clamp(p_color, vec3(0.0), vec3(1.0)));
	return srgb_to_linear(max(mix(encoded, camera_lut_sample(encoded), camera_lut_strength(p_packed)), vec3(0.0)));
}
