/* clang-format off */
#[vertex]

#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;
/* clang-format on */

void main() {
	// One triangle over the whole target; uv (0, 0) is the top-left of the image, as for texture reads.
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
	uv_interp = gl_Position.xy * 0.5 + 0.5;
}

/* clang-format off */
#[fragment]

#version 450

#VERSION_DEFINES

// Screen-space light shafts (Environment.light_shafts_*, from the DirectionalLight3D with light_shafts_enabled), after
// Mitchell, "Volumetric Light Scattering as a Post-Process", GPU Gems 3 ch. 13 (2007): a mask of the sky that is bright
// enough to scatter, blurred radially toward the sun's position on screen, added to the scene light. The radial blur is
// reformulated so its result does not depend on the sample count or the resolution (servers/rendering/storage/environment_storage.cpp has the model):
//
//   S(p) = decay * integral_0^L exp(-decay t) M(p + t u) dt + exp(-decay L) M(p + L u)
//
// with u the unit direction from p toward the sun and t measured in frame heights. L is the distance to the sun, or the
// march's cutoff when that is nearer (or the sun is a direction, for orthogonal cameras); the second term continues the
// ray at the sun itself, so the weights always sum to 1 and a pixel on the sun keeps the sun's own brightness.
//
// Passes, in order, at a quarter of the frame's width and height except the last:
//   MODE_MASK(_RESOLVED_DEPTH)  sky pixels above the threshold (scene-linear), 4x4 box-filtered.
//   MODE_BLUR                   S(p): the integral by stratified exponential importance sampling (equal weights).
//   MODE_COMPOSITE              S upsampled and added to the frame's color (additive blend, so no copy), times
//                               intensity, tint, the sun's fade and the luminance multiplier.

layout(location = 0) in vec2 uv_interp;
/* clang-format on */

#include "finite_inc.glsl"

layout(push_constant, std430) uniform Params {
	vec4 sun; // xy: the sun in uv when z is 1; when z is 0, the direction toward it in frame-height units. w: aspect.
	vec4 scale; // MODE_COMPOSITE: rgb added per unit of S.
	float threshold; // Scene-linear luminance.
	float decay; // Per frame height.
	float max_length; // Cutoff of the march, in frame heights.
	float luminance_multiplier; // The color buffer holds scene light times this (Mobile's RGB10A2 range).
	uint sample_count;
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

layout(location = 0) out vec4 frag_color;

#if defined(MODE_MASK) || defined(MODE_MASK_RESOLVED_DEPTH)

// With MSAA, the depth texture is the resolve target, a plain R32F texture holding depth values rather than a depth
// format, and WebGPU binds the two as different types. The driver tells them apart by name: anything called "depth"
// becomes texture_depth_2d. Same as CameraMotionBlur's MODE_PREP_RESOLVED_DEPTH.
#ifdef MODE_MASK_RESOLVED_DEPTH
#define depth_buffer resolved_z_source
#endif
layout(set = 0, binding = 0) uniform sampler2D source_color;
layout(set = 0, binding = 1) uniform sampler2D depth_buffer;

void main() {
	// Four bilinear taps, each the mean of a 2x2 block, cover the 4x4 source pixels under this mask pixel, so a sun disk
	// a few pixels across keeps its energy at quarter resolution.
	vec2 color_texel = 1.0 / vec2(textureSize(source_color, 0));
	ivec2 depth_size = textureSize(depth_buffer, 0);
	vec3 mask = vec3(0.0);
	for (int i = 0; i < 4; i++) {
		vec2 offset = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
		vec2 uv = uv_interp + offset * color_texel;
		// Reverse Z: the sky is at depth 0 (cleared, nothing drawn). Geometry, however far, occludes.
		float depth = texelFetch(depth_buffer, clamp(ivec2(uv * vec2(depth_size)), ivec2(0), depth_size - 1), 0).r;
		if (depth > 0.0) {
			continue;
		}
		vec3 color = finite_or_zero3(textureLod(source_color, uv, 0.0).rgb) / params.luminance_multiplier;
		// A soft knee on luminance keeps the hue: what is above the threshold scatters, scaled as the pixel's color.
		float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722)); // ITU-R BT.709 luma of linear light.
		mask += color * (max(luminance - params.threshold, 0.0) / max(luminance, 1e-4));
	}
	frag_color = vec4(mask * 0.25, 1.0);
}

#endif

#ifdef MODE_BLUR

layout(set = 0, binding = 0) uniform sampler2D mask_texture;

float interleaved_gradient_noise(vec2 p_pos) {
	// Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014).
	return fract(52.9829189 * fract(dot(p_pos, vec2(0.06711056, 0.00583715))));
}

vec3 sample_mask(vec2 p_pos) {
	// p_pos is in frame-height units. Outside the frame nothing is known of the sky, so it contributes nothing: an
	// off-screen sun's shafts come only from the sky the frame shows (and fade with the sun, light_shafts_compute_sun()).
	vec2 uv = vec2(p_pos.x / params.sun.w, p_pos.y);
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
		return vec3(0.0);
	}
	return textureLod(mask_texture, uv, 0.0).rgb;
}

void main() {
	float aspect = params.sun.w;
	vec2 pos = vec2(uv_interp.x * aspect, uv_interp.y);
	vec2 to_sun;
	float distance_to_sun;
	if (params.sun.z > 0.5) {
		to_sun = vec2(params.sun.x * aspect, params.sun.y) - pos;
		distance_to_sun = length(to_sun);
	} else {
		to_sun = params.sun.xy;
		distance_to_sun = 1e10;
	}
	if (distance_to_sun < 1e-6) {
		frag_color = vec4(sample_mask(pos), 1.0);
		return;
	}
	vec2 dir = to_sun / length(to_sun);
	float march = min(distance_to_sun, params.max_length);

	// t = -ln(1 - v (1 - exp(-decay L))) / decay has density decay exp(-decay t) / (1 - exp(-decay L)) on [0, L], so
	// samples at stratified v all weigh (1 - exp(-decay L)) / N. The jitter turns the banding of a fixed pattern into
	// noise finer than the bilinear upsample.
	float tail = exp(-params.decay * march);
	float jitter = interleaved_gradient_noise(gl_FragCoord.xy);
	float n = float(params.sample_count);
	vec3 sum = vec3(0.0);
	for (uint i = 0u; i < params.sample_count; i++) {
		float v = (float(i) + jitter) / n;
		float t = -log(1.0 - v * (1.0 - tail)) / params.decay;
		sum += sample_mask(pos + dir * t);
	}
	vec3 shaft = sum * ((1.0 - tail) / n) + sample_mask(pos + dir * march) * tail;
	frag_color = vec4(shaft, 1.0);
}

#endif

#ifdef MODE_COMPOSITE

layout(set = 0, binding = 0) uniform sampler2D shaft_texture;

void main() {
	frag_color = vec4(textureLod(shaft_texture, uv_interp, 0.0).rgb * params.scale.rgb, 0.0);
}

#endif
