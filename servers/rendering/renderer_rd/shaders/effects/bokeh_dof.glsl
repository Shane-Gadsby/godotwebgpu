#[compute]

#version 450

#VERSION_DEFINES

#define BLOCK_SIZE 8

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

#ifdef MODE_GEN_BLUR_SIZE
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
// DEPTH_IS_STORAGE_FORMAT (BokehDOF::BOKEH_GEN_BLUR_SIZE_RESOLVED_DEPTH, bokeh_dof.h/.cpp):
// renames the depth-reading sampler so this driver's depth-texture reclassification
// heuristic (Task 7.13, keyed off "depth" appearing in the WGSL variable name) leaves it
// as a plain texture_2d<f32> instead of promoting it to texture_depth_2d. Used when the
// real bound resource is RenderSceneBuffersRD::get_depth_texture()'s MSAA-resolve-target
// form -- a plain R32Float storage texture holding real depth VALUES (written by
// Resolve::resolve_depth()/resolve_gi()), not a native hardware depth FORMAT -- which a
// Depth-sampleType binding cannot accept on WebGPU. Renaming (rather than passing a
// runtime flag) is required because this genuinely needs different WGSL, not just a
// different bound resource: the two cases share the exact same GLSL access pattern
// (texelFetch(source_depth, ...).r), so nothing but the variable's name can tell this
// pass which WGSL type is actually correct for a given compiled variant. See
// webgpu_notes/TASKS.md Task 24 round 4.
#ifdef DEPTH_IS_STORAGE_FORMAT
#define source_depth resolved_z_source
#endif
layout(set = 1, binding = 0) uniform sampler2D source_depth;
#endif

#if defined(MODE_BOKEH_BOX) || defined(MODE_BOKEH_HEXAGONAL) || defined(MODE_BOKEH_CIRCULAR)
layout(set = 1, binding = 0) uniform sampler2D color_texture;
layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D bokeh_image;
#endif

#ifdef MODE_COMPOSITE_BOKEH
layout(rgba16f, set = 0, binding = 0) uniform restrict image2D color_image;
layout(set = 1, binding = 0) uniform sampler2D source_bokeh;
#endif

// based on https://www.shadertoy.com/view/Xd3GDl

#include "bokeh_dof_inc.glsl"

#ifdef MODE_GEN_BLUR_SIZE

float get_depth_at_pos(vec2 uv) {
	float depth = textureLod(source_depth, uv, 0.0).x * 2.0 - 1.0;
	if (params.orthogonal) {
		depth = -(depth * (params.z_far - params.z_near) - (params.z_far + params.z_near)) / 2.0;
	} else {
		depth = 2.0 * params.z_near * params.z_far / (params.z_far + params.z_near + depth * (params.z_far - params.z_near));
	}
	return depth;
}

float get_blur_size(float depth, vec2 uv) {
	float near_begin = params.blur_near_begin;
	float far_begin = params.blur_far_begin;
	float size_near = params.blur_size_near;
	float size_far = params.blur_size_far;
	if ((params.shape_flags & BOKEH_SHAPE_TILT) != 0u) {
		// Tilt-shift: every pixel has its own focus distance (bokeh_dof_inc.glsl).
		float scale;
		far_begin = bokeh_tilt_focus_distance(uv, params.blur_far_begin, scale);
		near_begin = far_begin;
		size_near *= scale;
		size_far *= scale;
	}
	if (params.blur_near_active && depth < near_begin) {
		if (params.use_physical_near) {
			// Physically-based.
			float d = abs(near_begin - depth);
			return -(d / (near_begin - d)) * size_near - DEPTH_GAP; // Near blur is negative.
		} else {
			// Non-physically-based.
			return -(1.0 - smoothstep(params.blur_near_end, params.blur_near_begin, depth)) * params.blur_size - DEPTH_GAP; // Near blur is negative.
		}
	}

	if (params.blur_far_active && depth > far_begin) {
		if (params.use_physical_far) {
			// Physically-based.
			float d = abs(far_begin - depth);
			return (d / (far_begin + d)) * size_far + DEPTH_GAP;
		} else {
			// Non-physically-based.
			return smoothstep(params.blur_far_begin, params.blur_far_end, depth) * params.blur_size + DEPTH_GAP;
		}
	}

	return 0.0;
}

#endif

#if defined(MODE_BOKEH_BOX) || defined(MODE_BOKEH_HEXAGONAL)

vec4 weighted_filter_dir(vec2 dir, vec2 uv, vec2 pixel_size) {
	dir *= pixel_size;
	vec4 color = texture(color_texture, uv);

	vec4 accum = color;
	float total = 1.0;

	float blur_scale = bokeh_gather_reach(params.blur_size) / float(params.blur_steps);
	bool physical_size = (params.shape_flags & BOKEH_PHYSICAL_SIZE) != 0u;
	float limit_scale = bokeh_physical_size_limit_scale();
	vec4 dev = vec4(0.0);
	float largest_blur = abs(color.a) * limit_scale;

	if (params.use_jitter) {
		uv += dir * (hash12n(uv + params.jitter_seed) - 0.5);
	}

	for (int i = -params.blur_steps; i <= params.blur_steps; i++) {
		if (i == 0) {
			continue;
		}
		float radius = float(i) * blur_scale;
		vec2 suv = uv + dir * radius;
		radius = abs(radius);

		vec4 sample_color = texture(color_texture, suv);
		float limit;

		if (sample_color.a < color.a) {
			limit = abs(sample_color.a);
		} else {
			limit = abs(color.a);
		}

		limit -= DEPTH_GAP;
		limit *= limit_scale;

		float m = smoothstep(radius - 0.5, radius + 0.5, limit);
		if (m > 0.0) {
			largest_blur = max(largest_blur, limit);
		}

		if ((params.shape_flags & BOKEH_LONGITUDINAL_CA) != 0u) {
			// Red and blue have their own circles of confusion (longitudinal chromatic aberration).
			float sample_rb = bokeh_blur_red_blue(sample_color.a);
			float color_rb = bokeh_blur_red_blue(color.a);
			float limit_rb = (abs(sample_rb < color_rb ? sample_rb : color_rb) - DEPTH_GAP) * limit_scale;
			float m_rb = smoothstep(radius - 0.5, radius + 0.5, limit_rb);
			if (m_rb > 0.0) {
				largest_blur = max(largest_blur, limit_rb);
			}
			vec4 mv = vec4(m_rb, m, m_rb, m);
			accum += mix(color, sample_color, mv);
			dev += mv * (sample_color - color);
		} else {
			accum += mix(color, sample_color, m);
			dev += m * (sample_color - color);
		}

		total += 1.0;
	}

	if (physical_size) {
		return bokeh_physical_size_resolve(color, dev, largest_blur, blur_scale);
	}
	return accum / total;
}

#endif

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	if (any(greaterThan(pos, params.size))) { //too large, do nothing
		return;
	}

	vec2 pixel_size = 1.0 / vec2(params.size);
	vec2 uv = vec2(pos) / vec2(params.size);

#ifdef MODE_GEN_BLUR_SIZE
	uv += pixel_size * 0.5;
	//precompute size in alpha channel
	float depth = get_depth_at_pos(uv);
	float size = get_blur_size(depth, uv);

	vec4 color = imageLoad(color_image, pos);
	// Every later pass reads this image, so sanitizing here, once per pixel, covers them all.
	color.rgb = finite_or_zero3(color.rgb);
	color.a = size;
	imageStore(color_image, pos, color);
#endif

#ifdef MODE_BOKEH_BOX

	//pixel_size*=0.5; //resolution is doubled
	if (params.second_pass || !params.half_size) {
		uv += pixel_size * 0.5; //half pixel to read centers
	} else {
		uv += pixel_size * 0.25; //half pixel to read centers from full res
	}

	vec2 dir = (params.second_pass ? vec2(0.0, 1.0) : vec2(1.0, 0.0));

	vec4 color = weighted_filter_dir(dir, uv, pixel_size);

	imageStore(bokeh_image, pos, color);

#endif

#ifdef MODE_BOKEH_HEXAGONAL

	//pixel_size*=0.5; //resolution is doubled
	if (params.second_pass || !params.half_size) {
		uv += pixel_size * 0.5; //half pixel to read centers
	} else {
		uv += pixel_size * 0.25; //half pixel to read centers from full res
	}

	vec2 dir = (params.second_pass ? normalize(vec2(1.0, 0.577350269189626)) : vec2(0.0, 1.0));

	vec4 color = weighted_filter_dir(dir, uv, pixel_size);

	if (params.second_pass) {
		dir = normalize(vec2(-1.0, 0.577350269189626));

		vec4 color2 = weighted_filter_dir(dir, uv, pixel_size);

		color.rgb = min(color.rgb, color2.rgb);
		color.a = (color.a + color2.a) * 0.5;
	}

	imageStore(bokeh_image, pos, color);

#endif

#ifdef MODE_BOKEH_CIRCULAR

	if (params.half_size) {
		pixel_size *= 0.5; //resolution is doubled
	}

	uv += pixel_size * 0.5; //half pixel to read centers

	vec4 color = texture(color_texture, uv);
	float initial_blur = color.a;
	float initial_blur_rb = bokeh_blur_red_blue(initial_blur);
	float accum = 1.0;
	float radius = params.blur_scale;
	float reach = bokeh_gather_reach(params.blur_size);

	for (float ang = 0.0; radius < reach; ang += GOLDEN_ANGLE) {
		vec2 suv = uv + vec2(cos(ang), sin(ang)) * pixel_size * radius;
		vec4 sample_color = texture(color_texture, suv);
		float sample_size = abs(sample_color.a);
		if (sample_color.a > initial_blur) {
			sample_size = clamp(sample_size, 0.0, abs(initial_blur) * 2.0);
		}

		float shape_extent = 1.0;
		if ((params.shape_flags & BOKEH_SHAPE_CUSTOM) != 0u) {
			// The sample's bokeh reaches this pixel only if it lies inside the aperture's shape.
			shape_extent = bokeh_shape_extent(-vec2(cos(ang), sin(ang)), suv, sample_color.a < 0.0);
			sample_size *= shape_extent;
		}
		float m = smoothstep(radius - 0.5, radius + 0.5, sample_size);
		if ((params.shape_flags & BOKEH_LONGITUDINAL_CA) != 0u) {
			// Red and blue have their own circles of confusion (longitudinal chromatic aberration).
			float sample_rb = bokeh_blur_red_blue(sample_color.a);
			float sample_size_rb = abs(sample_rb);
			if (sample_rb > initial_blur_rb) {
				sample_size_rb = clamp(sample_size_rb, 0.0, abs(initial_blur_rb) * 2.0);
			}
			float m_rb = smoothstep(radius - 0.5, radius + 0.5, sample_size_rb * shape_extent);
			color += mix(color / accum, sample_color, vec4(m_rb, m, m_rb, m));
		} else {
			color += mix(color / accum, sample_color, m);
		}
		accum += 1.0;
		radius += params.blur_scale / radius;
	}

	color /= accum;

	imageStore(bokeh_image, pos, color);
#endif

#ifdef MODE_COMPOSITE_BOKEH

	uv += pixel_size * 0.5;
	vec4 color = imageLoad(color_image, pos);
	vec4 bokeh = texture(source_bokeh, uv);

	float mix_amount;
	if (bokeh.a < color.a) {
		mix_amount = min(1.0, max(0.0, max(abs(color.a), abs(bokeh.a)) - DEPTH_GAP));
	} else {
		mix_amount = min(1.0, max(0.0, abs(color.a) - DEPTH_GAP));
	}

	if ((params.shape_flags & BOKEH_LONGITUDINAL_CA) != 0u) {
		// Red and blue blend by their own circles of confusion (longitudinal chromatic aberration).
		float color_rb = bokeh_blur_red_blue(color.a);
		float bokeh_rb = bokeh_blur_red_blue(bokeh.a);
		float mix_rb;
		if (bokeh_rb < color_rb) {
			mix_rb = min(1.0, max(0.0, max(abs(color_rb), abs(bokeh_rb)) - DEPTH_GAP));
		} else {
			mix_rb = min(1.0, max(0.0, abs(color_rb) - DEPTH_GAP));
		}
		color.rgb = mix(color.rgb, bokeh.rgb, vec3(mix_rb, mix_amount, mix_rb));
	} else {
		color.rgb = mix(color.rgb, bokeh.rgb, mix_amount); //blend between hires and lowres
	}

	color.a = 0; //reset alpha
	imageStore(color_image, pos, color);
#endif
}
