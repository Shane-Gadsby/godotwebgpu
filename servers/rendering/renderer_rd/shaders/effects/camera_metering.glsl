#[compute]

#version 450

#VERSION_DEFINES

// CameraAttributes exposure automation (CameraMetering, camera_metering.cpp; the model is in
// camera_attributes_storage.cpp, "Exposure automation" and "Auto white balance"). Forward+ only.
//
// Auto exposure metering other than Average (which is luminance_reduce.glsl):
// - Center-weighted and spot: MODE_METER_READ writes per 8x8 block the sums of w * v and w, where v = max(R, G, B) and
//   w the pixel's metering weight; MODE_REDUCE sums 8x8 blocks of those down to 1x1; MODE_METER_RESOLVE divides,
//   adapts toward it from the previous luminance as luminance_reduce.glsl does, and writes the 1x1 luminance the
//   tonemapper reads. Sums rather than means, and blocks that round up, so every pixel counts exactly once.
// - Highlight-weighted: MODE_HISTOGRAM counts log2(v) into 256 bins, MODE_HISTOGRAM_RESOLVE finds the percentile.
// Auto white balance: MODE_AWB_READ writes the sums of w * RGB and w, w a pixel's grayness, MODE_REDUCE sums them, and
// MODE_AWB_RESOLVE turns them into the illuminant estimate, adapts the state toward it and writes the cone gains.
// The state is a 2x1 RGBA32F texture: texel 0 the gains, texel 1 (u, v, valid, 0), the adapted illuminant in CIE 1960
// UCS. Two of them alternate, the previous one read while the next one is written.

#include "finite_inc.glsl"

#if defined(MODE_HISTOGRAM)
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
#elif defined(MODE_METER_READ) || defined(MODE_AWB_READ) || defined(MODE_REDUCE)
#define BLOCK_SIZE 8
layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;
#else
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	float adjust; // Adaptation this frame: auto exposure's speed * delta, auto white balance's 1 - exp(-speed * delta).
	uint flags;

	float min_luminance;
	float max_luminance;
	float highlight_percentile; // Fraction of the pixels at or below the metered value.
	float highlight_key; // The metered value is percentile * key.

	vec2 center; // Metering circle, in units of the frame height from the top-left corner.
	float radius;
	float inner_weight;

	float outer_weight;
	float exposure; // Auto white balance: the tonemapper's exposure, to tell which pixels clip.
	float auto_exposure_scale;
	float luminance_multiplier;

	float min_temperature;
	float max_temperature;
	float pad[2];
}
params;

#define FLAG_SET 1u // Start over: no adaptation from the previous state.
#define FLAG_USE_AUTO_EXPOSURE 2u
// The weighted log-average, exp(sum(w log(v + delta)) / sum(w)), instead of the weighted mean (Reinhard et al. 2002's
// key, as luminance_reduce.glsl's FLAG_LOG_AVERAGE does for Average). A percentile is the same either way.
#define FLAG_LOG_AVERAGE 4u
#define LOG_AVERAGE_DELTA 1e-4

// The log2 histogram: bin 0 holds everything below 2^-16 (black included), bins 1-254 are 1/8 EV wide, bin 255 holds
// everything from 2^15.75 up.
#define HISTOGRAM_BINS 256u
#define HISTOGRAM_LOG2_MIN -16.0
#define HISTOGRAM_BIN_WIDTH 0.125

/* Bindings: set 0 the inputs, set 1 the output. */

#if defined(MODE_METER_READ) || defined(MODE_AWB_READ) || defined(MODE_HISTOGRAM)
layout(set = 0, binding = 0) uniform sampler2D source_color;
#endif

#if defined(MODE_REDUCE) || defined(MODE_METER_RESOLVE) || defined(MODE_AWB_RESOLVE)
layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image2D source_sums;
#endif

#ifdef MODE_HISTOGRAM_RESOLVE
layout(set = 0, binding = 0, std430) restrict readonly buffer HistogramIn {
	uint data[];
}
histogram_in;
#endif

#if defined(MODE_METER_RESOLVE) || defined(MODE_HISTOGRAM_RESOLVE)
layout(set = 0, binding = 1) uniform sampler2D prev_luminance;
layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_luminance;
#endif

#if defined(MODE_AWB_READ) || defined(MODE_AWB_RESOLVE)
layout(set = 0, binding = 1) uniform sampler2D prev_state;
#endif

#ifdef MODE_AWB_READ
layout(set = 0, binding = 2) uniform sampler2D source_auto_exposure;
#endif

#if defined(MODE_METER_READ) || defined(MODE_AWB_READ) || defined(MODE_REDUCE)
layout(rgba32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_sums;
shared vec4 tmp_data[BLOCK_SIZE * BLOCK_SIZE];
#endif

#ifdef MODE_HISTOGRAM
layout(set = 1, binding = 0, std430) restrict buffer HistogramOut {
	uint data[];
}
histogram_out;
shared uint local_bins[HISTOGRAM_BINS];
#endif

#ifdef MODE_AWB_RESOLVE
layout(rgba32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_state;
#endif

#if defined(MODE_AWB_READ) || defined(MODE_AWB_RESOLVE)
// The white balance's cone space, as in tonemap.glsl: BRADFORD * SRGB_TO_XYZ.
const mat3 linear_srgb_to_lms = mat3(
		vec3(0.422725293, 0.055699777, 0.021382644),
		vec3(0.491345324, 0.961534051, 0.087641868),
		vec3(0.027357945, 0.023183810, 0.980508133));
const mat3 lms_to_linear_srgb = mat3(
		vec3(2.538044517, -0.146004058, -0.042298510),
		vec3(-1.293276998, 1.116648306, -0.071607220),
		vec3(-0.040236884, -0.022329026, 1.022752688));
#endif

#ifdef MODE_AWB_RESOLVE
// Bradford (Lam 1985), XYZ -> LMS, and its inverse.
const mat3 bradford = mat3(
		vec3(0.8951, -0.7502, 0.0389),
		vec3(0.2664, 1.7135, -0.0685),
		vec3(-0.1614, 0.0367, 1.0296));
const mat3 bradford_inverse = mat3(
		vec3(0.986992905, 0.432305270, -0.008528665),
		vec3(-0.147054256, 0.518360272, 0.040042822),
		vec3(0.159962652, 0.049291228, 0.968486696));

// The Planckian locus in CIE 1960 UCS (Krystek 1985), as RendererCameraAttributes and Light3D::light_temperature use it.
vec2 planck_uv(float p_temperature) {
	float t = p_temperature;
	float t2 = t * t;
	return vec2((0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2),
			(0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2));
}

// The isotherm direction toward green (larger v), as the manual tint uses it. +-10 K keeps float cancellation small.
vec2 planck_normal(float p_temperature) {
	vec2 d = planck_uv(p_temperature + 10.0) - planck_uv(p_temperature - 10.0);
	return normalize(vec2(d.y, -d.x));
}

vec2 xyz_to_uv(vec3 p_xyz) {
	float d = p_xyz.x + 15.0 * p_xyz.y + 3.0 * p_xyz.z;
	return vec2(4.0 * p_xyz.x, 6.0 * p_xyz.y) / max(d, 1e-20);
}

// XYZ with Y = 1.
vec3 uv_to_xyz(vec2 p_uv) {
	float d = 2.0 * p_uv.x - 8.0 * p_uv.y + 4.0;
	float x = 3.0 * p_uv.x / d;
	float y = max(2.0 * p_uv.y / d, 1e-6);
	return vec3(x / y, 1.0, (1.0 - x - y) / y);
}
#endif

void main() {
#if defined(MODE_METER_READ) || defined(MODE_AWB_READ) || defined(MODE_REDUCE)
	uint t = gl_LocalInvocationID.y * BLOCK_SIZE + gl_LocalInvocationID.x;
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	vec4 value = vec4(0.0);

	if (all(lessThan(pos, params.source_size))) {
#ifdef MODE_METER_READ
		vec3 c = finite_or_zero3(texelFetch(source_color, pos, 0).rgb);
		float v = max(c.r, max(c.g, c.b));
		vec2 q = (vec2(pos) + 0.5) / float(params.source_size.y);
		float w = distance(q, params.center) <= params.radius ? params.inner_weight : params.outer_weight;
		if (bool(params.flags & FLAG_LOG_AVERAGE)) {
			v = log(v + LOG_AVERAGE_DELTA);
		}
		value = vec4(w * v, w, 0.0, 0.0);
#endif
#ifdef MODE_AWB_READ
		vec3 c = finite_or_zero3(texelFetch(source_color, pos, 0).rgb);
		// The exposure the tonemapper gives this pixel: what clips there is no evidence of the illuminant.
		float exposure = params.luminance_multiplier * params.exposure;
		if (bool(params.flags & FLAG_USE_AUTO_EXPOSURE)) {
			exposure *= 1.0 / (texelFetch(source_auto_exposure, ivec2(0, 0), 0).r * params.luminance_multiplier / params.auto_exposure_scale);
		}
		vec3 exposed = c * exposure;
		if (max(exposed.r, max(exposed.g, exposed.b)) < 1.0) {
			// Grayness after the current correction (the previous frame's gains).
			vec3 gains = vec3(1.0);
			if (!bool(params.flags & FLAG_SET) && texelFetch(prev_state, ivec2(1, 0), 0).z > 0.5) {
				gains = texelFetch(prev_state, ivec2(0, 0), 0).rgb;
			}
			vec3 b = lms_to_linear_srgb * (gains * (linear_srgb_to_lms * c));
			if (all(greaterThan(b, vec3(0.0)))) {
				const float sigma = 0.25;
				vec2 d = log(b.rb / b.g);
				float w = exp(-0.5 * dot(d, d) / (sigma * sigma));
				value = vec4(c * w, w);
			}
		}
#endif
#ifdef MODE_REDUCE
		value = imageLoad(source_sums, pos);
#endif
	}

	tmp_data[t] = value;
	groupMemoryBarrier();
	barrier();

	uint size = (BLOCK_SIZE * BLOCK_SIZE) >> 1;
	do {
		if (t < size) {
			tmp_data[t] += tmp_data[t + size];
		}
		groupMemoryBarrier();
		barrier();
		size >>= 1;
	} while (size >= 1);

	if (t == 0) {
		imageStore(dest_sums, pos / ivec2(BLOCK_SIZE), tmp_data[0]);
	}
#endif

#ifdef MODE_HISTOGRAM
	uint local_index = gl_LocalInvocationIndex;
	local_bins[local_index] = 0u;
	barrier();

	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (all(lessThan(pos, params.source_size))) {
		vec3 c = finite_or_zero3(texelFetch(source_color, pos, 0).rgb);
		float v = max(c.r, max(c.g, c.b));
		uint bin = 0u;
		if (v >= exp2(HISTOGRAM_LOG2_MIN)) {
			bin = min(1u + uint(floor((log2(v) - HISTOGRAM_LOG2_MIN) / HISTOGRAM_BIN_WIDTH)), HISTOGRAM_BINS - 1u);
		}
		atomicAdd(local_bins[bin], 1u);
	}
	barrier();

	uint count = local_bins[local_index];
	if (count != 0u) {
		atomicAdd(histogram_out.data[local_index], count);
	}
#endif

#if defined(MODE_METER_RESOLVE) || defined(MODE_HISTOGRAM_RESOLVE)
	float avg = 0.0;
#ifdef MODE_METER_RESOLVE
	vec4 sums = imageLoad(source_sums, ivec2(0, 0));
	avg = sums.y > 0.0 ? sums.x / sums.y : 0.0;
	if (bool(params.flags & FLAG_LOG_AVERAGE)) {
		avg = sums.y > 0.0 ? exp(avg) : 0.0;
	}
#else
	float total = 0.0;
	for (uint i = 0u; i < HISTOGRAM_BINS; i++) {
		total += float(histogram_in.data[i]);
	}
	float target = params.highlight_percentile * total;
	float below = 0.0;
	float log2_value = HISTOGRAM_LOG2_MIN;
	for (uint i = 0u; i < HISTOGRAM_BINS; i++) {
		float n = float(histogram_in.data[i]);
		if (n > 0.0 && below + n >= target) {
			// Spread evenly in log2 within the bin.
			float frac = clamp((target - below) / n, 0.0, 1.0);
			if (i == 0u) {
				log2_value = HISTOGRAM_LOG2_MIN;
			} else if (i == HISTOGRAM_BINS - 1u) {
				log2_value = HISTOGRAM_LOG2_MIN + float(HISTOGRAM_BINS - 2u) * HISTOGRAM_BIN_WIDTH;
			} else {
				log2_value = HISTOGRAM_LOG2_MIN + (float(i - 1u) + frac) * HISTOGRAM_BIN_WIDTH;
			}
			break;
		}
		below += n;
	}
	avg = exp2(log2_value) * params.highlight_key;
#endif
	// The adaptation and clamp of luminance_reduce.glsl's WRITE_LUMINANCE, so every mode moves alike.
	if (!bool(params.flags & FLAG_SET)) {
		float prev_lum = texelFetch(prev_luminance, ivec2(0, 0), 0).r;
		avg = clamp(prev_lum + (avg - prev_lum) * params.adjust, params.min_luminance, params.max_luminance);
	}
	imageStore(dest_luminance, ivec2(0, 0), vec4(avg));
#endif

#ifdef MODE_AWB_RESOLVE
	vec3 white_lms = linear_srgb_to_lms * vec3(1.0); // Display white, R = G = B, where a gray card should land.
	vec3 white_xyz = bradford_inverse * white_lms;
	white_lms /= white_xyz.y;

	vec4 prev = texelFetch(prev_state, ivec2(1, 0), 0);
	vec2 uv = xyz_to_uv(white_xyz);
	if (!bool(params.flags & FLAG_SET) && prev.z > 0.5) {
		uv = prev.xy;
	}

	vec4 sums = imageLoad(source_sums, ivec2(0, 0));
	if (sums.w > 0.0 && all(greaterThan(sums.rgb, vec3(0.0)))) {
		vec2 target = xyz_to_uv(bradford_inverse * (linear_srgb_to_lms * (sums.rgb / sums.w)));

		// Correlated color temperature: the nearest point of the locus, by golden-section search in mireds.
		float lo = 1e6 / 15000.0;
		float hi = 1e6 / 1000.0;
		const float golden = 0.618034;
		float m1 = hi - golden * (hi - lo);
		float m2 = lo + golden * (hi - lo);
		vec2 d1 = target - planck_uv(1e6 / m1);
		vec2 d2 = target - planck_uv(1e6 / m2);
		float f1 = dot(d1, d1);
		float f2 = dot(d2, d2);
		for (int i = 0; i < 48; i++) {
			if (f1 < f2) {
				hi = m2;
				m2 = m1;
				f2 = f1;
				m1 = hi - golden * (hi - lo);
				d1 = target - planck_uv(1e6 / m1);
				f1 = dot(d1, d1);
			} else {
				lo = m1;
				m1 = m2;
				f1 = f2;
				m2 = lo + golden * (hi - lo);
				d2 = target - planck_uv(1e6 / m2);
				f2 = dot(d2, d2);
			}
		}
		float temperature = 1e6 / (0.5 * (lo + hi));
		vec2 normal = planck_normal(temperature);
		float duv = dot(target - planck_uv(temperature), normal);

		// Outside the range: the nearest illuminant inside it. Inside, the estimate itself, untouched.
		float clamped_temperature = clamp(temperature, params.min_temperature, params.max_temperature);
		float clamped_duv = clamp(duv, -0.05, 0.05);
		if (clamped_temperature != temperature || clamped_duv != duv) {
			target = planck_uv(clamped_temperature) + planck_normal(clamped_temperature) * clamped_duv;
		}
		uv = mix(uv, target, params.adjust);
	}

	vec3 gains = white_lms / (bradford * uv_to_xyz(uv));
	imageStore(dest_state, ivec2(0, 0), vec4(gains, 1.0));
	imageStore(dest_state, ivec2(1, 0), vec4(uv, 1.0, 0.0));
#endif
}
