#[vertex]

#version 450

#VERSION_DEFINES

void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
}

#[fragment]

#version 450

#VERSION_DEFINES

// Keep in sync with CameraFinish::FinishPushConstant (camera_finish.h).
#define MODE_NOISE_REDUCTION 0
#define MODE_SHARPEN 1
#define MODE_OVERLAY 2

#define FLAG_LINEAR_INPUT (1 << 0)
#define FLAG_NR_CHROMA_ONLY (1 << 1)
#define FLAG_ZEBRAS (1 << 2)
#define FLAG_ZEBRAS_SECOND (1 << 3)
#define FLAG_FOCUS_PEAKING (1 << 4)
#define FLAG_FALSE_COLOR (1 << 5)
#define FLAG_HISTOGRAM (1 << 6)
#define FLAG_HISTOGRAM_RGB (1 << 7)

layout(push_constant, std430) uniform Params {
	vec2 pixel_size;
	uint mode;
	uint flags;

	float nr_spatial_sigma;
	float nr_range_sigma;
	float sharpen_amount;
	float sharpen_sigma;

	float sharpen_halo;
	float time;
	float zebras_level; // Luma, 0-1.
	float zebras_second_level;

	float stripe_period; // Pixels.
	float peaking_threshold;
	float histogram_opacity;
	float pad;

	vec4 peaking_color;
	vec4 histogram_rect; // x0, y0, x1, y1 in pixels, y down.

	vec4 zebras_color; // Display-encoded; .a unused.
	vec4 zebras_second_color;
}
params;

layout(set = 0, binding = 0) uniform sampler2D source_color;

// Written by camera_histogram.glsl: 256 bins of R', G', B' and Y' interleaved, then the largest bin of each.
layout(set = 0, binding = 1, std430) restrict readonly buffer Histogram {
	uint data[];
}
histogram;

layout(location = 0) out vec4 frag_color;

#define CF_SOURCE source_color
#define CF_PIXEL_SIZE params.pixel_size
#define CF_LINEAR_INPUT bool(params.flags & FLAG_LINEAR_INPUT)

#include "camera_finish_inc.glsl"

void main() {
	vec2 pos = gl_FragCoord.xy;
	vec4 source = textureLod(source_color, pos * params.pixel_size, 0.0);

	if (params.mode == MODE_NOISE_REDUCTION) {
		vec3 c = cf_noise_reduction(pos, params.nr_spatial_sigma, params.nr_range_sigma, bool(params.flags & FLAG_NR_CHROMA_ONLY));
		frag_color = vec4(cf_from_display(c), source.a);
		return;
	}
	if (params.mode == MODE_SHARPEN) {
		vec3 c = cf_sharpen(pos, params.sharpen_amount, params.sharpen_sigma, params.sharpen_halo);
		frag_color = vec4(cf_from_display(c), source.a);
		return;
	}

	// Overlays, measured on the display-encoded image as a camera's monitor measures its video signal.
	vec3 display = cf_to_display(source.rgb);
	float luma = dot(clamp(display, vec3(0.0), vec3(1.0)), CF_LUMA_709);
	vec3 result = display;

	if (bool(params.flags & FLAG_FALSE_COLOR)) {
		result = cf_false_color(luma * 100.0);
	}
	if (bool(params.flags & FLAG_ZEBRAS_SECOND) && abs(luma - params.zebras_second_level) <= 0.05 && cf_zebra_stripe(pos, params.stripe_period, params.time, -1.0)) {
		result = params.zebras_second_color.rgb;
	}
	if (bool(params.flags & FLAG_ZEBRAS) && luma >= params.zebras_level && cf_zebra_stripe(pos, params.stripe_period, params.time, 1.0)) {
		result = params.zebras_color.rgb;
	}
	if (bool(params.flags & FLAG_FOCUS_PEAKING) && cf_peaking_gradient(pos) >= params.peaking_threshold) {
		result = params.peaking_color.rgb;
	}

	if (bool(params.flags & FLAG_HISTOGRAM)) {
		vec4 rect = params.histogram_rect;
		if (pos.x >= rect.x && pos.x < rect.z && pos.y >= rect.y && pos.y < rect.w) {
			uint bin = min(uint((pos.x - rect.x) / (rect.z - rect.x) * 256.0), 255u);
			// Bar height as a fraction of the rect, measured up from its bottom edge at pixel centers.
			float height = (rect.w - pos.y) / (rect.w - rect.y);
			vec3 bars;
			if (bool(params.flags & FLAG_HISTOGRAM_RGB)) {
				for (uint c = 0u; c < 3u; c++) {
					float value = float(histogram.data[bin * 4u + c]) / float(max(histogram.data[1024u + c], 1u));
					bars[c] = height <= value ? 1.0 : 0.0;
				}
			} else {
				float value = float(histogram.data[bin * 4u + 3u]) / float(max(histogram.data[1027u], 1u));
				bars = vec3(height <= value ? 0.9 : 0.0);
			}
			result = mix(result, bars, params.histogram_opacity);
		}
	}

	frag_color = vec4(cf_from_display(result), source.a);
}
