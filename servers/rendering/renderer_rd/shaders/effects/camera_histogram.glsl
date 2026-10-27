#[compute]

#version 450

#VERSION_DEFINES

// The monitoring histogram of Camera3D: 256 bins, one per 8-bit code value, of the display-encoded R', G', B' and
// Rec. 709 Y' (ITU-R BT.709-6), counted on the GPU. The buffer holds the four channels interleaved (bin * 4 + channel),
// then the largest bin of each channel at 1024 + channel, which the MODE_MAX dispatch finds and the overlay draws
// against. CameraFinish clears it before each frame's MODE_COUNT dispatch.

#ifdef MODE_MAX
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 size;
	uint linear_input;
	uint pad;
}
params;

#ifndef MODE_MAX
layout(set = 0, binding = 0) uniform sampler2D source_color;
#endif

// Set 0 binding 1 in both modes, so the MAX dispatch, which reads no texture, still uses set 0 only.
layout(set = 0, binding = 1, std430) restrict buffer Histogram {
	uint data[];
}
histogram;

#ifdef MODE_MAX
shared uint channel_max[4];
#else
shared uint local_bins[1024];
#endif

vec3 srgb_encode(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, 12.92 * c, lessThan(c, vec3(0.0031308)));
}

void main() {
	uint local_index = gl_LocalInvocationIndex;

#ifdef MODE_MAX
	if (local_index < 4u) {
		channel_max[local_index] = 0u;
	}
	barrier();
	for (uint c = 0u; c < 4u; c++) {
		atomicMax(channel_max[c], histogram.data[local_index * 4u + c]);
	}
	barrier();
	if (local_index < 4u) {
		histogram.data[1024u + local_index] = channel_max[local_index];
	}
#else
	for (uint i = local_index; i < 1024u; i += 256u) {
		local_bins[i] = 0u;
	}
	barrier();

	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	if (pos.x < params.size.x && pos.y < params.size.y) {
		vec3 c = texelFetch(source_color, pos, 0).rgb;
		if (params.linear_input != 0u) {
			c = srgb_encode(c);
		}
		c = clamp(c, vec3(0.0), vec3(1.0));
		float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
		uvec4 bins = uvec4(round(vec4(c, luma) * 255.0));
		atomicAdd(local_bins[bins.r * 4u + 0u], 1u);
		atomicAdd(local_bins[bins.g * 4u + 1u], 1u);
		atomicAdd(local_bins[bins.b * 4u + 2u], 1u);
		atomicAdd(local_bins[bins.a * 4u + 3u], 1u);
	}
	barrier();

	for (uint i = local_index; i < 1024u; i += 256u) {
		uint count = local_bins[i];
		if (count != 0u) {
			atomicAdd(histogram.data[i], count);
		}
	}
#endif
}
