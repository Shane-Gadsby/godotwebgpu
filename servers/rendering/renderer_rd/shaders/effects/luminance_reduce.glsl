#[compute]

#version 450

#VERSION_DEFINES

#include "finite_inc.glsl"

#define BLOCK_SIZE 8

// Average auto exposure metering: the mean of max(R, G, B) over every pixel of the frame. Each level writes the sum of
// its 8x8 block (pixels outside the source count as nothing), every level is its source's size / 8 rounded up, and the
// last level divides by the frame's pixel count, so every pixel carries the same weight wherever it is. (Upstream wrote
// each block's mean into levels rounded down, which dropped the right and bottom edge blocks at every level: the right
// 20% of a 1080p frame and the bottom 27% of a 720p one never reached the meter.)
// FLAG_LOG_AVERAGE meters the log-average instead, exp(mean(log(v + delta))) (Reinhard et al. 2002, "Photographic
// Tone Reproduction for Digital Images", eq. 1, the scene's "key"); see camera_attributes_storage.cpp.
#define FLAG_LOG_AVERAGE 1u
#define LOG_AVERAGE_DELTA 1e-4

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

shared float tmp_data[BLOCK_SIZE * BLOCK_SIZE];

#ifdef READ_TEXTURE

//use for main texture
layout(set = 0, binding = 0) uniform sampler2D source_texture;

#else

//use for intermediate textures
layout(r32f, set = 0, binding = 0) uniform restrict readonly image2D source_luminance;

#endif

layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_luminance;

#ifdef WRITE_LUMINANCE
layout(set = 2, binding = 0) uniform sampler2D prev_luminance;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	float max_luminance;
	float min_luminance;
	float exposure_adjust;
	float inv_pixel_count; // 1 / the frame's pixel count on the last level, which turns the sum into the mean; 0 before it.
	uint flags;
	float pad;
}
params;

void main() {
	uint t = gl_LocalInvocationID.y * BLOCK_SIZE + gl_LocalInvocationID.x;
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	if (all(lessThan(pos, params.source_size))) {
#ifdef READ_TEXTURE
		// The scene color: a non-finite pixel counts as black, or the sums below would make the
		// average, and with it the exposure of the whole frame (and of every frame after it, through
		// the previous luminance), NaN (finite_inc.glsl).
		vec3 v = finite_or_zero3(texelFetch(source_texture, pos, 0).rgb);
		tmp_data[t] = max(v.r, max(v.g, v.b));
		if (bool(params.flags & FLAG_LOG_AVERAGE)) {
			tmp_data[t] = log(tmp_data[t] + LOG_AVERAGE_DELTA);
		}
#else
		tmp_data[t] = imageLoad(source_luminance, pos).r;
#endif
	} else {
		tmp_data[t] = 0.0;
	}

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
		float avg = tmp_data[0];
		if (params.inv_pixel_count > 0.0) {
			avg *= params.inv_pixel_count;
			if (bool(params.flags & FLAG_LOG_AVERAGE)) {
				avg = exp(avg);
			}
		}
		pos /= ivec2(BLOCK_SIZE);
#ifdef WRITE_LUMINANCE
		float prev_lum = texelFetch(prev_luminance, ivec2(0, 0), 0).r; //1 pixel previous exposure
		avg = clamp(prev_lum + (avg - prev_lum) * params.exposure_adjust, params.min_luminance, params.max_luminance);
#endif
		imageStore(dest_luminance, pos, vec4(avg));
	}
}
