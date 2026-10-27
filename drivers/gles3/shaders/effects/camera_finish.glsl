/* clang-format off */
#[modes]

mode_noise_reduction = #define MODE_NOISE_REDUCTION
mode_sharpen = #define MODE_SHARPEN
mode_overlay = #define MODE_OVERLAY

#[specializations]

LINEAR_INPUT = false

#[vertex]
layout(location = 0) in vec2 vertex_attrib;

/* clang-format on */

void main() {
	gl_Position = vec4(vertex_attrib, 1.0, 1.0);
}

/* clang-format off */
#[fragment]
/* clang-format on */

// The camera finish pass for Compatibility (GLES3::CameraFinish): the RD pass's noise reduction, sharpening and
// overlays, without the histogram, which needs compute. See servers/rendering/renderer_rd/shaders/effects/camera_finish.glsl.

#define FLAG_NR_CHROMA_ONLY 2
#define FLAG_ZEBRAS 4
#define FLAG_ZEBRAS_SECOND 8
#define FLAG_FOCUS_PEAKING 16
#define FLAG_FALSE_COLOR 32

uniform sampler2D source_color; // texunit:0
uniform vec2 pixel_size;
uniform int flags;
uniform float nr_spatial_sigma;
uniform float nr_range_sigma;
uniform float sharpen_amount;
uniform float sharpen_sigma;
uniform float sharpen_halo;
uniform float time;
uniform float zebras_level;
uniform float zebras_second_level;
uniform float stripe_period;
uniform float peaking_threshold;
uniform vec3 peaking_color;

layout(location = 0) out vec4 frag_color;

#define CF_SOURCE source_color
#define CF_PIXEL_SIZE pixel_size
#ifdef LINEAR_INPUT
#define CF_LINEAR_INPUT true
#else
#define CF_LINEAR_INPUT false
#endif

#include "camera_finish_inc.glsl"

void main() {
	vec2 pos = gl_FragCoord.xy;
	vec4 source = textureLod(source_color, pos * pixel_size, 0.0);

#ifdef MODE_NOISE_REDUCTION
	vec3 c = cf_noise_reduction(pos, nr_spatial_sigma, nr_range_sigma, (flags & FLAG_NR_CHROMA_ONLY) != 0);
	frag_color = vec4(cf_from_display(c), source.a);
#endif

#ifdef MODE_SHARPEN
	vec3 c = cf_sharpen(pos, sharpen_amount, sharpen_sigma, sharpen_halo);
	frag_color = vec4(cf_from_display(c), source.a);
#endif

#ifdef MODE_OVERLAY
	vec3 display = cf_to_display(source.rgb);
	float luma = dot(clamp(display, vec3(0.0), vec3(1.0)), CF_LUMA_709);
	vec3 result = display;

	if ((flags & FLAG_FALSE_COLOR) != 0) {
		result = cf_false_color(luma * 100.0);
	}
	// GL's window origin is the bottom left: flip y so the stripes run the same way as in the RD renderers.
	vec2 stripe_pos = vec2(pos.x, 1.0 / pixel_size.y - pos.y);
	if ((flags & FLAG_ZEBRAS_SECOND) != 0 && abs(luma - zebras_second_level) <= 0.05 && cf_zebra_stripe(stripe_pos, stripe_period, time, -1.0)) {
		result = vec3(1.0);
	}
	if ((flags & FLAG_ZEBRAS) != 0 && luma >= zebras_level && cf_zebra_stripe(stripe_pos, stripe_period, time, 1.0)) {
		result = vec3(0.0);
	}
	if ((flags & FLAG_FOCUS_PEAKING) != 0 && cf_peaking_gradient(pos) >= peaking_threshold) {
		result = peaking_color;
	}
	frag_color = vec4(cf_from_display(result), source.a);
#endif
}
