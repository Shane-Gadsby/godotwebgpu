#[compute]

#version 450

#VERSION_DEFINES

// The Environment's color grading LUT, finished for one tonemap pass (RendererRD::ColorGrading, color_grading.cpp).
// The CPU bakes the grade itself (RendererColorGrading::bake(), servers/rendering/storage/color_grading.cpp) into
// source_grading: lattice points, sRGB-encoded in and out. This pass composes it with the Environment's own color
// correction LUT, when there is one, exactly as the tonemap pass would sample that LUT after the grade (grading first,
// then the user's LUT), and writes the result the tonemap pass reads in the color correction slot. It runs only when
// either input changes.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

layout(set = 0, binding = 0) uniform sampler3D source_grading;
#ifdef MODE_USER_LUT_1D
layout(set = 0, binding = 1) uniform sampler2D source_user_lut;
#else
layout(set = 0, binding = 1) uniform sampler3D source_user_lut;
#endif
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image3D dest_lut;

#define FLAG_USE_USER_LUT 1u
// The tonemap pass outputs linear values (HDR 2D or HDR output): without a user LUT, decode here, as sampling the
// user LUT's sRGB view does for it.
#define FLAG_LINEARIZE 2u

layout(push_constant, std430) uniform Params {
	uint flags;
	uint size;
	uint pad[2];
}
params;

vec3 srgb_to_linear(vec3 color) {
	const vec3 a = vec3(0.055f);
	return mix(pow((color.rgb + a) * (1.0f / (vec3(1.0f) + a)), vec3(2.4f)), color.rgb * (1.0f / 12.92f), lessThan(color.rgb, vec3(0.04045f)));
}

void main() {
	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
	if (any(greaterThanEqual(pos, ivec3(params.size)))) {
		return;
	}
	vec3 color = texelFetch(source_grading, pos, 0).rgb;
	if (bool(params.flags & FLAG_USE_USER_LUT)) {
		// As tonemap.glsl's apply_color_correction() for a user LUT: clamped, plain coordinates.
		color = clamp(color, vec3(0.0), vec3(1.0));
#ifdef MODE_USER_LUT_1D
		color.r = textureLod(source_user_lut, vec2(color.r, 0.0), 0.0).r;
		color.g = textureLod(source_user_lut, vec2(color.g, 0.0), 0.0).g;
		color.b = textureLod(source_user_lut, vec2(color.b, 0.0), 0.0).b;
#else
		color = textureLod(source_user_lut, color, 0.0).rgb;
#endif
	} else if (bool(params.flags & FLAG_LINEARIZE)) {
		color = srgb_to_linear(color);
	}
	imageStore(dest_lut, pos, vec4(color, 1.0));
}
