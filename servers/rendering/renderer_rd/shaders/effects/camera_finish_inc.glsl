// Camera finish: in-camera noise reduction and sharpening (CameraAttributes) and the monitoring overlays (Camera3D),
// applied after tonemapping at the output resolution. Shared by the RD pass (camera_finish.glsl) and Compatibility
// (drivers/gles3/shaders/effects/camera_finish.glsl, a copy of this file); keep both copies identical.
//
// The including shader defines, before including this file:
//   CF_SOURCE         the sampler2D holding the tonemapped image (sampled with linear filtering, clamped);
//   CF_PIXEL_SIZE     vec2, 1.0 / image size in pixels;
//   CF_LINEAR_INPUT   bool expression, true when the image holds linear values (an HDR 2D render target) rather
//                     than sRGB-encoded ones. Everything here works on the display encoding, as a camera does.
//
// The models and their sources are in servers/rendering/storage/camera_attributes_storage.cpp ("Camera finish") and
// in the comments below.

// Rec. 709 luma coefficients (ITU-R BT.709-6, table 3, items 3.2 and 3.3), applied to the display-encoded R'G'B'.
const vec3 CF_LUMA_709 = vec3(0.2126, 0.7152, 0.0722);

vec3 cf_srgb_encode(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c, lessThan(c, vec3(0.0031308)));
}

vec3 cf_srgb_decode(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(pow((c + 0.055) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
}

vec3 cf_to_display(vec3 c) {
	return (CF_LINEAR_INPUT) ? cf_srgb_encode(c) : c;
}

vec3 cf_from_display(vec3 c) {
	return (CF_LINEAR_INPUT) ? cf_srgb_decode(c) : c;
}

// Y'CbCr with the Rec. 709 matrix (BT.709-6 table 3, items 3.2-3.3): Cb = (B' - Y') / 1.8556, Cr = (R' - Y') / 1.5748.
vec3 cf_rgb_to_ycc(vec3 c) {
	float y = dot(c, CF_LUMA_709);
	return vec3(y, (c.b - y) * (1.0 / 1.8556), (c.r - y) * (1.0 / 1.5748));
}

vec3 cf_ycc_to_rgb(vec3 v) {
	float r = v.x + 1.5748 * v.z;
	float b = v.x + 1.8556 * v.y;
	float g = (v.x - 0.2126 * r - 0.0722 * b) * (1.0 / 0.7152);
	return vec3(r, g, b);
}

// The display-encoded color at a position in pixels (pixel centers at .5).
vec3 cf_fetch(vec2 p_pos) {
	return cf_to_display(textureLod(CF_SOURCE, p_pos * CF_PIXEL_SIZE, 0.0).rgb);
}

// Kernel taps per side for a Gaussian of p_sigma pixels: out to 2 sigma, at most 4 taps (9x9), spaced evenly. Wider
// kernels space their taps more than a pixel apart; the bilinear fetch between them keeps that from aliasing.
#define CF_MAX_TAPS 4

int cf_taps(float p_sigma) {
	return clamp(int(ceil(2.0 * p_sigma)), 1, CF_MAX_TAPS);
}

// Noise reduction: a bilateral filter (Tomasi and Manduchi 1998) on Y'CbCr. Luma uses p_range_sigma on the luma
// difference; chroma uses twice that on the whole Y'CbCr difference, so it is reduced harder but still stops at luma
// edges, as cameras do. p_chroma_only leaves luma untouched.
vec3 cf_noise_reduction(vec2 p_pos, float p_spatial_sigma, float p_range_sigma, bool p_chroma_only) {
	vec3 center = cf_rgb_to_ycc(cf_fetch(p_pos));
	int taps = cf_taps(p_spatial_sigma);
	float step_px = 2.0 * p_spatial_sigma / float(taps);
	float inv_2ss = 1.0 / (2.0 * p_spatial_sigma * p_spatial_sigma);
	float inv_2rl = 1.0 / (2.0 * p_range_sigma * p_range_sigma);
	float inv_2rc = inv_2rl * 0.25; // Chroma range sigma is twice the luma's.

	float luma_sum = 0.0;
	float luma_weight = 0.0;
	vec2 chroma_sum = vec2(0.0);
	float chroma_weight = 0.0;
	for (int j = -CF_MAX_TAPS; j <= CF_MAX_TAPS; j++) {
		for (int i = -CF_MAX_TAPS; i <= CF_MAX_TAPS; i++) {
			if (abs(i) > taps || abs(j) > taps) {
				continue;
			}
			vec2 offset = vec2(float(i), float(j)) * step_px;
			vec3 tap = cf_rgb_to_ycc(cf_fetch(p_pos + offset));
			vec3 d = tap - center;
			float ws = exp(-dot(offset, offset) * inv_2ss);
			float wl = ws * exp(-d.x * d.x * inv_2rl);
			float wc = ws * exp(-dot(d, d) * inv_2rc);
			luma_sum += tap.x * wl;
			luma_weight += wl;
			chroma_sum += tap.yz * wc;
			chroma_weight += wc;
		}
	}
	vec3 result;
	result.x = p_chroma_only ? center.x : luma_sum / luma_weight;
	result.yz = chroma_sum / chroma_weight;
	return cf_ycc_to_rgb(result);
}

// Sharpening: an unsharp mask on luma, Y' + amount * (Y' - Gaussian(Y')), added equally to R'G'B'. The result is held
// within the 3x3 neighborhood's luma range (taps one sigma apart, at least a pixel) widened by p_halo times that range:
// the ringing limit of AMD FidelityFX CAS (Lottes 2019).
vec3 cf_sharpen(vec2 p_pos, float p_amount, float p_sigma, float p_halo) {
	vec3 center = cf_fetch(p_pos);
	float y = dot(center, CF_LUMA_709);
	int taps = cf_taps(p_sigma);
	float step_px = 2.0 * p_sigma / float(taps);
	float inv_2ss = 1.0 / (2.0 * p_sigma * p_sigma);

	float blur = 0.0;
	float weight = 0.0;
	for (int j = -CF_MAX_TAPS; j <= CF_MAX_TAPS; j++) {
		for (int i = -CF_MAX_TAPS; i <= CF_MAX_TAPS; i++) {
			if (abs(i) > taps || abs(j) > taps) {
				continue;
			}
			vec2 offset = vec2(float(i), float(j)) * step_px;
			float w = exp(-dot(offset, offset) * inv_2ss);
			blur += dot(cf_fetch(p_pos + offset), CF_LUMA_709) * w;
			weight += w;
		}
	}
	blur /= weight;

	float reach = max(1.0, p_sigma);
	float lo = y;
	float hi = y;
	for (int j = -1; j <= 1; j++) {
		for (int i = -1; i <= 1; i++) {
			float n = dot(cf_fetch(p_pos + vec2(float(i), float(j)) * reach), CF_LUMA_709);
			lo = min(lo, n);
			hi = max(hi, n);
		}
	}
	float range = hi - lo;
	float sharpened = clamp(y + p_amount * (y - blur), lo - p_halo * range, hi + p_halo * range);
	return center + vec3(sharpened - y);
}

// False color: the ARRI ALEXA false color bands on the video signal level (ARRI, "ALEXA Mini / ALEXA SXT Software
// Update Packet" user manuals, "False Color" exposure check; the same table on the ALEXA 35 and AMIRA):
//   purple  0.0 - 2.5 %   black clipping
//   blue    2.5 - 4.0 %   just above black clipping
//   green  38.0 - 42.0 %  18 % neutral gray
//   pink   52.0 - 56.0 %  one stop over 18 % gray (typical light skin)
//   yellow 97.0 - 99.0 %  just below white clipping
//   red    99.0 - 100 %   white clipping
// Everything else shows as gray at its own luma. The band colors are ARRI's as displayed, approximated.
vec3 cf_false_color(float p_ire) {
	if (p_ire < 2.5) {
		return vec3(0.5, 0.0, 0.75);
	} else if (p_ire < 4.0) {
		return vec3(0.0, 0.35, 1.0);
	} else if (p_ire >= 38.0 && p_ire < 42.0) {
		return vec3(0.0, 0.8, 0.0);
	} else if (p_ire >= 52.0 && p_ire < 56.0) {
		return vec3(1.0, 0.5, 0.75);
	} else if (p_ire >= 97.0 && p_ire < 99.0) {
		return vec3(1.0, 1.0, 0.0);
	} else if (p_ire >= 99.0) {
		return vec3(1.0, 0.0, 0.0);
	}
	return vec3(p_ire * 0.01);
}

// Zebras: 45-degree stripes, half on and half off, crawling along their normal by one period a second as camera
// zebras do. The period is a fraction of the frame height, so they look the same at any resolution. p_direction
// flips the diagonal (the second zebra runs the other way). Returns true where a stripe is drawn.
bool cf_zebra_stripe(vec2 p_pos, float p_period, float p_time, float p_direction) {
	float phase = (p_pos.x + p_direction * p_pos.y) / (p_period * 1.41421356) - p_time;
	return fract(phase) < 0.5;
}

// Focus peaking: the Sobel gradient of luma, normalized so a full-scale one-pixel step reads 1.0. Detail in focus has
// steep gradients; defocus spreads the same step over the blur circle and lowers them.
float cf_peaking_gradient(vec2 p_pos) {
	float l[9];
	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 3; i++) {
			l[j * 3 + i] = dot(clamp(cf_fetch(p_pos + vec2(float(i - 1), float(j - 1))), vec3(0.0), vec3(1.0)), CF_LUMA_709);
		}
	}
	float gx = (l[2] + 2.0 * l[5] + l[8]) - (l[0] + 2.0 * l[3] + l[6]);
	float gy = (l[6] + 2.0 * l[7] + l[8]) - (l[0] + 2.0 * l[1] + l[2]);
	return length(vec2(gx, gy)) * 0.25;
}
