#[compute]

#version 450

#VERSION_DEFINES

// Camera motion blur and rolling shutter (CameraAttributes.motion_blur_*, rolling_shutter_*; the model is described
// in camera_attributes_storage.cpp). Motion blur is the reconstruction filter of McGuire, Hennessy, Bukowski and
// Osman, "A Reconstruction Filter for Plausible Motion Blur" (I3D 2012): per-pixel blur vectors, the largest one per
// tile of the frame and its 3x3 neighborhood, then a gather along the neighborhood's dominant blur. The taps are
// weighted as in Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014): each
// tap stands for an equal share of the exposure, and counts if it is in front and its own blur reaches this pixel,
// or behind and this pixel's blur reaches it; the shares that do not count go to the pixel itself. Unlike McGuire's
// cone weights, that gives the exact result of a box shutter for an object moving over a still background (a linear
// ramp over the blur length, centered on the edge), measured against it in results/34-camera-motion.
//
// Passes, in order:
//   MODE_PREP          velocity buffer + depth -> each pixel's motion over one frame, in output pixels, and its
//                      linear depth. Pixels without geometry (the sky) get the camera's motion, derived from depth.
//   MODE_TILE_MAX      the longest blur vector of each tile (one workgroup per tile).
//   MODE_NEIGHBOR_MAX  the longest of each tile's 3x3 neighborhood.
//   MODE_GATHER        rolling shutter (each row shown at its own time) and the blur itself.
//
// Everything is in output pixels of the color buffer being blurred, converted from the velocity buffer's UV units, so
// the blur covers the same fraction of the frame at every resolution.

#define BLOCK_SIZE 8

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	mat4 reprojection; // Current clip space to the previous frame's (MODE_PREP).
	vec2 size; // Of the color buffer, in pixels.
	ivec2 tile_count;
	float blur_scale; // Blur length per unit of a frame's motion: exposure time / frame time * strength.
	float max_radius; // Half the longest blur, in pixels.
	float rolling_shutter; // Readout time from the top row to the bottom one, in frames.
	float z_near;
	float z_far;
	uint tile_size; // In pixels; at least max_radius, as the filter requires.
	uint sample_count;
	uint frame;
	bool orthogonal;
	uint pad[3];
}
params;

#if defined(MODE_PREP) || defined(MODE_PREP_RESOLVED_DEPTH)

#include "motion_vector_inc.glsl"

// With MSAA, the depth texture is the resolve target, a plain R32F texture holding depth values rather than a depth
// format, and WebGPU binds the two as different types. The driver tells them apart by name: anything called "depth"
// becomes texture_depth_2d. Same as BokehDOF's BOKEH_GEN_BLUR_SIZE_RESOLVED_DEPTH.
#ifdef MODE_PREP_RESOLVED_DEPTH
#define depth_buffer resolved_z_source
#endif
layout(set = 0, binding = 0) uniform sampler2D depth_buffer;
layout(set = 0, binding = 1) uniform sampler2D velocity_texture;
layout(rgba16f, set = 0, binding = 2) uniform restrict writeonly image2D prep_image;

float linearize_depth(float p_depth) {
	float depth = p_depth * 2.0 - 1.0;
	if (params.orthogonal) {
		return -(depth * (params.z_far - params.z_near) - (params.z_far + params.z_near)) / 2.0;
	}
	return 2.0 * params.z_near * params.z_far / (params.z_far + params.z_near + depth * (params.z_far - params.z_near));
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, ivec2(params.size)))) {
		return;
	}
	vec2 uv = (vec2(pos) + 0.5) / params.size;

	// The depth and velocity buffers are at the internal resolution, which differs from the output's with
	// temporal upscaling.
	ivec2 depth_size = textureSize(depth_buffer, 0);
	float depth = texelFetch(depth_buffer, clamp(ivec2(uv * vec2(depth_size)), ivec2(0), depth_size - 1), 0).r;

	vec2 velocity; // Previous UV - current UV.
	if (depth <= 0.0) {
		// Reverse Z: nothing was drawn here. The sky writes no motion vectors, but it moves when the camera turns.
		velocity = derive_motion_vector(uv, depth, params.reprojection);
	} else {
		ivec2 velocity_size = textureSize(velocity_texture, 0);
		velocity = texelFetch(velocity_texture, clamp(ivec2(uv * vec2(velocity_size)), ivec2(0), velocity_size - 1), 0).xy;
	}

	// Stored as the direction of motion over the last frame, in output pixels.
	imageStore(prep_image, pos, vec4(-velocity * params.size, linearize_depth(depth), 0.0));
}

#endif

#if defined(MODE_TILE_MAX) || defined(MODE_GATHER)

// Half the blur a pixel moving by p_motion pixels per frame gets (the exposure is centered on the frame's time),
// limited to the longest blur allowed.
vec2 blur_radius(vec2 p_motion) {
	vec2 radius = p_motion * (params.blur_scale * 0.5);
	float len = length(radius);
	return len > params.max_radius ? radius * (params.max_radius / len) : radius;
}

#endif

#ifdef MODE_TILE_MAX

layout(set = 0, binding = 0) uniform sampler2D prep_texture;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D tile_image;

shared vec3 tile_max_shared[BLOCK_SIZE * BLOCK_SIZE];

void main() {
	// One workgroup per tile. The loops run the same number of times on every invocation (barriers need uniform
	// control flow); bounds are checked inside.
	ivec2 origin = ivec2(gl_WorkGroupID.xy) * int(params.tile_size);
	vec3 best = vec3(0.0);
	for (uint y = 0; y < params.tile_size; y += BLOCK_SIZE) {
		for (uint x = 0; x < params.tile_size; x += BLOCK_SIZE) {
			uvec2 local = uvec2(x, y) + gl_LocalInvocationID.xy;
			ivec2 pos = origin + ivec2(local);
			if (all(lessThan(local, uvec2(params.tile_size))) && all(lessThan(pos, ivec2(params.size)))) {
				vec2 radius = blur_radius(texelFetch(prep_texture, pos, 0).xy);
				float len2 = dot(radius, radius);
				if (len2 > best.z) {
					best = vec3(radius, len2);
				}
			}
		}
	}

	uint index = gl_LocalInvocationIndex;
	tile_max_shared[index] = best;
	barrier();
	for (uint stride = (BLOCK_SIZE * BLOCK_SIZE) / 2; stride > 0; stride >>= 1) {
		if (index < stride && tile_max_shared[index + stride].z > tile_max_shared[index].z) {
			tile_max_shared[index] = tile_max_shared[index + stride];
		}
		barrier();
	}

	if (index == 0) {
		imageStore(tile_image, ivec2(gl_WorkGroupID.xy), vec4(tile_max_shared[0].xy, 0.0, 0.0));
	}
}

#endif

#ifdef MODE_NEIGHBOR_MAX

layout(set = 0, binding = 0) uniform sampler2D tile_texture;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D neighbor_image;

void main() {
	ivec2 tile = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(tile, params.tile_count))) {
		return;
	}
	// Tiles are as large as the longest blur, so only the eight neighbors can reach into this one.
	vec2 best = vec2(0.0);
	float best_len2 = 0.0;
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			vec2 radius = texelFetch(tile_texture, clamp(tile + ivec2(x, y), ivec2(0), params.tile_count - 1), 0).xy;
			float len2 = dot(radius, radius);
			if (len2 > best_len2) {
				best = radius;
				best_len2 = len2;
			}
		}
	}
	imageStore(neighbor_image, tile, vec4(best, 0.0, 0.0));
}

#endif

#ifdef MODE_GATHER

layout(set = 0, binding = 0) uniform sampler2D color_texture;
layout(set = 0, binding = 1) uniform sampler2D prep_texture;
layout(set = 0, binding = 2) uniform sampler2D neighbor_texture;
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D output_image;

// Interleaved gradient noise (Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare",
// SIGGRAPH 2014), offset per frame: the tap positions' jitter, which trades the filter's banding for fine noise.
float gradient_noise(vec2 p_pos) {
	p_pos += float(params.frame & 63u) * 5.588238;
	return fract(52.9829189 * fract(dot(p_pos, vec2(0.06711056, 0.00583715))));
}

// How much a tap at depth p_tap is behind (x) and in front of (y) the pixel at p_center: 0.5 each at the same
// depth, so the two always sum to 1. The softness, 2% of the depth, is a look choice: McGuire et al. and Jimenez
// use a fixed scene distance, which cannot suit both a close-up and a landscape.
vec2 depth_compare(float p_center, float p_tap) {
	float d = (p_tap - p_center) / (0.02 * min(p_center, p_tap) + 1e-4);
	return clamp(vec2(0.5 + d, 0.5 - d), 0.0, 1.0);
}

// 1 while a blur of radius p_radius reaches p_dist, fading over one pixel (Jimenez's SpreadCmp).
float spread_compare(float p_dist, float p_radius) {
	return clamp(1.0 + p_radius - p_dist, 0.0, 1.0);
}

vec4 prep_at(ivec2 p_pos) {
	return texelFetch(prep_texture, clamp(p_pos, ivec2(0), ivec2(params.size) - 1), 0);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos, ivec2(params.size)))) {
		return;
	}
	vec2 center = vec2(pos) + 0.5;

	// Rolling shutter: this row was exposed rolling_shutter * (row / height - 0.5) frames from the frame's time (top
	// row first, the middle row unchanged), when the content now at c was at c + motion(c) * t. Solve for the c
	// that lands here by fixed-point iteration, starting from this pixel's own motion. Content the velocity buffer
	// does not have (what a moving object uncovers) cannot be shown; it repeats what is there.
	if (params.rolling_shutter != 0.0) {
		float row_time = (center.y / params.size.y - 0.5) * params.rolling_shutter;
		vec2 source = center - prep_at(pos).xy * row_time;
		source = center - prep_at(ivec2(floor(source))).xy * row_time;
		center = clamp(source, vec2(0.5), params.size - 0.5);
	}

	ivec2 x = ivec2(floor(center));
	vec4 color = textureLod(color_texture, center / params.size, 0.0);

	if (params.blur_scale > 0.0) {
		vec2 neighbor_max = texelFetch(neighbor_texture, clamp(x / int(params.tile_size), ivec2(0), params.tile_count - 1), 0).xy;
		if (dot(neighbor_max, neighbor_max) > 0.25) { // Blurs under half a pixel leave the pixel as it is.
			vec4 prep_x = prep_at(x);
			float radius_x = length(blur_radius(prep_x.xy));

			float total_weight = 0.0;
			vec4 sum = vec4(0.0);
			float jitter = gradient_noise(vec2(pos)) - 0.5;
			for (uint i = 0; i < params.sample_count; i++) {
				// One jittered tap per equal slice of the neighborhood's blur, both ways from the pixel. (McGuire et
				// al. space them (i + 1) / (S + 1), which leaves the ends of the blur out: blurs came out S / (S + 1)
				// of their length.)
				float t = -1.0 + 2.0 * (float(i) + 0.5 + jitter) / float(params.sample_count);
				ivec2 y = clamp(ivec2(floor(vec2(x) + 0.5 + neighbor_max * t)), ivec2(0), ivec2(params.size) - 1);
				vec4 prep_y = prep_at(y);
				float radius_y = length(blur_radius(prep_y.xy));
				float dist = length(vec2(y - x));

				// A tap behind shows while this pixel's own blur moves away from it; a tap in front covers this
				// pixel while its blur passes over it. At the same depth both halves apply.
				vec2 behind_front = depth_compare(prep_x.z, prep_y.z);
				float weight = behind_front.x * spread_compare(dist, radius_x) + behind_front.y * spread_compare(dist, radius_y);
				total_weight += weight;
				sum += texelFetch(color_texture, y, 0) * weight;
			}
			// Each tap is 1 / sample_count of the exposure; what no tap claims is the pixel's own color.
			color = (sum + color * (float(params.sample_count) - total_weight)) / float(params.sample_count);
		}
	}

	imageStore(output_image, pos, color);
}

#endif
