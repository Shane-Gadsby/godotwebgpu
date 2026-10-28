///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ground Truth based Ambient Occlusion (GTAO), an alternative to ASSAO (ssao.glsl) selected by the project setting
// rendering/environment/ssao/method. It writes the same R8 texture (RB_SCOPE_SSAO/RB_FINAL) the scene shader reads as
// ao_buffer, so nothing downstream changes.
//
// The algorithm is Jimenez, Wu, Pesce, Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion"
// (Activision, SIGGRAPH 2016 course), ported from Intel's XeGTAO reference implementation:
//
//   XeGTAO, https://github.com/GameTechDev/XeGTAO (XeGTAO.hlsli)
//   Copyright (C) 2016-2021, Intel Corporation
//   SPDX-License-Identifier: MIT
//   Author: Filip Strugar (filip.strugar@intel.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Changes for Godot: GLSL compute, one shader with five modes (depth prefilter, main pass, denoise, final denoise,
// depth-aware upsample from half resolution), view-space normals from the normal_roughness buffer instead of normals
// reconstructed from depth, orthographic projections, and the Environment's SSAO intensity/power/fade applied after
// the visibility term the way ASSAO applies them. Visibility bent normals are not computed (nothing reads them).
//
// View space here is ASSAO's: x right, y up, +z away from the camera (positive linear depth), the same space
// ssao.glsl's NDC_to_view_space() and load_normal() use. XeGTAO uses the same convention.
//
// WebGPU: the linear view-depth texture is deliberately not named "*depth*": the WebGPU driver turns a single-channel
// texture_2d whose name contains "depth" into texture_depth_2d (wgsl_text_scan.cpp), and this one is an R16F color
// texture. source_depth below *is* the hardware depth buffer and is meant to be reclassified.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#[compute]

#version 450

#VERSION_DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#define XE_GTAO_PI (3.1415926535897932384626433832795)
#define XE_GTAO_PI_HALF (1.5707963267948966192313216916398)
#define XE_GTAO_DEPTH_MIP_LEVELS 5.0

#if defined(MODE_PREFILTER) || defined(MODE_UPSAMPLE)
// The hardware depth buffer, full resolution.
layout(set = 0, binding = 0) uniform sampler2D source_depth;
#endif

#ifdef MODE_PREFILTER
layout(r16f, set = 1, binding = 0) uniform restrict writeonly image2D dest_view_z0;
layout(r16f, set = 1, binding = 1) uniform restrict writeonly image2D dest_view_z1;
layout(r16f, set = 1, binding = 2) uniform restrict writeonly image2D dest_view_z2;
layout(r16f, set = 1, binding = 3) uniform restrict writeonly image2D dest_view_z3;
layout(r16f, set = 1, binding = 4) uniform restrict writeonly image2D dest_view_z4;
#endif

#ifdef MODE_MAIN
// Linear view-space depth at the working resolution, with XE_GTAO_DEPTH_MIP_LEVELS mips (MODE_PREFILTER's output),
// read with texelFetch only.
layout(set = 0, binding = 0) uniform sampler2D src_view_z;
layout(rgba8, set = 0, binding = 1) uniform restrict readonly image2D source_normal;
// x: visibility, y: packed depth edges (left, right, top, bottom; 2 bits each) for the denoiser.
layout(rg8, set = 1, binding = 0) uniform restrict writeonly image2D dest_ao;
#endif

#if defined(MODE_DENOISE) || defined(MODE_UPSAMPLE)
layout(set = 0, binding = 1) uniform sampler2D src_ao;
#endif

#ifdef MODE_UPSAMPLE
layout(set = 0, binding = 2) uniform sampler2D src_view_z;
#endif

#if (defined(MODE_DENOISE) && defined(FINAL_APPLY)) || defined(MODE_UPSAMPLE)
// RB_SCOPE_SSAO/RB_FINAL. R8 must match the texture's real format (WebGPU validates it at bind time, see the
// note at ssao_allocate_buffers()).
layout(r8, set = 1, binding = 0) uniform restrict writeonly image2D dest_final;
#elif defined(MODE_DENOISE)
layout(rg8, set = 1, binding = 0) uniform restrict writeonly image2D dest_ao;
#endif

#ifdef MODE_MAIN
layout(push_constant, std430) uniform Params {
	ivec2 size; // Working resolution.
	vec2 full_pixel_size; // 1 / full resolution.

	vec2 ndc_to_view_mul;
	vec2 ndc_to_view_add;

	vec2 ndc_to_view_mul_x_pixel_size; // ndc_to_view_mul * working pixel size.
	float effect_radius; // Environment radius * XeGTAO radius multiplier.
	float falloff_mul;

	float falloff_add;
	float final_value_power;
	float sample_distribution_power;
	float depth_mip_sampling_offset;

	float intensity; // Environment intensity relative to its default (2.0): scales the occlusion.
	float fade_out_mul;
	float fade_out_add;
	uint noise_index;

	int slice_count;
	int steps_per_slice;
	uint is_orthogonal;
	int scale; // Full resolution pixels per working pixel (1 or 2).

	float sky_view_z; // Just short of the far plane: anything at or past it (or not finite) is background.
	float pad0;
	float pad1;
	float pad2;
}
params;
#else
layout(push_constant, std430) uniform Params {
	ivec2 size; // Working resolution (MODE_UPSAMPLE: full resolution).
	ivec2 src_size; // MODE_PREFILTER: full resolution; MODE_UPSAMPLE: working resolution.

	float z_mul; // Depth linearization, as ss_effects_downsample.glsl.
	float z_add;
	uint is_orthogonal;
	int scale; // Full resolution pixels per working pixel (1 or 2).

	float falloff_mul; // MODE_PREFILTER: XeGTAO_DepthMIPFilter() falloff.
	float falloff_add;
	float blur_beta; // MODE_DENOISE: center weight; larger blurs less (XeGTAO DenoiseBlurBeta).
	float edge_bias; // 1 - Environment sharpness, added to the edge weights as ASSAO's blur does.
}
params;
#endif

#if defined(MODE_PREFILTER) || defined(MODE_UPSAMPLE)
// Same linearization as ss_effects_downsample.glsl.
float screen_space_to_view_space_depth(float p_depth) {
	if (bool(params.is_orthogonal)) {
		float depth = p_depth * 2.0 - 1.0;
		return -(depth * (params.z_add - params.z_mul) - (params.z_add + params.z_mul)) / 2.0;
	}
	return params.z_mul / (params.z_add - p_depth);
}

float load_view_z_full(ivec2 p_pos) {
	p_pos = clamp(p_pos, ivec2(0), params.src_size - 1);
	float z = screen_space_to_view_space_depth(texelFetch(source_depth, p_pos, 0).x);
	// Keep it finite and inside R16F's range (65504): an infinite or NaN background depth would turn every average
	// and edge it touches into NaN, which WGSL's clamp() does not scrub the way Vulkan drivers happen to.
	return (z < 65000.0) ? max(z, 0.0) : 65000.0;
}
#endif

#if defined(MODE_DENOISE) || defined(MODE_UPSAMPLE)
vec2 load_ao(ivec2 p_pos, ivec2 p_size) {
	return texelFetch(src_ao, clamp(p_pos, ivec2(0), p_size - 1), 0).xy;
}
#endif

#ifdef MODE_PREFILTER

shared float g_scratch_depths[8][8];

// XeGTAO_DepthMIPFilter(): a weighted average that favors the nearer samples, so a far-away background does not
// pull a thin foreground occluder's depth back in the lower mips.
float depth_mip_filter(float p_depth0, float p_depth1, float p_depth2, float p_depth3) {
	float max_depth = max(max(p_depth0, p_depth1), max(p_depth2, p_depth3));
	float weight0 = clamp((max_depth - p_depth0) * params.falloff_mul + params.falloff_add, 0.0, 1.0);
	float weight1 = clamp((max_depth - p_depth1) * params.falloff_mul + params.falloff_add, 0.0, 1.0);
	float weight2 = clamp((max_depth - p_depth2) * params.falloff_mul + params.falloff_add, 0.0, 1.0);
	float weight3 = clamp((max_depth - p_depth3) * params.falloff_mul + params.falloff_add, 0.0, 1.0);
	float weight_sum = weight0 + weight1 + weight2 + weight3;
	return (weight0 * p_depth0 + weight1 * p_depth1 + weight2 * p_depth2 + weight3 * p_depth3) / weight_sum;
}

// XeGTAO_PrefilterDepths16x16(): every 8x8 group writes a 16x16 tile of mip 0 and the four mips below it.
// Stores outside a mip are discarded, so no thread returns early (they all have to reach the barriers).
void main() {
	uvec2 base_coord = gl_GlobalInvocationID.xy;
	uvec2 group_thread = gl_LocalInvocationID.xy;
	ivec2 pix_coord = ivec2(base_coord * 2u);

	// Half resolution takes every second full resolution depth (the same texel the main pass takes its normal from).
	float depth0 = load_view_z_full((pix_coord + ivec2(0, 0)) * params.scale);
	float depth1 = load_view_z_full((pix_coord + ivec2(1, 0)) * params.scale);
	float depth2 = load_view_z_full((pix_coord + ivec2(0, 1)) * params.scale);
	float depth3 = load_view_z_full((pix_coord + ivec2(1, 1)) * params.scale);

	imageStore(dest_view_z0, pix_coord + ivec2(0, 0), vec4(depth0));
	imageStore(dest_view_z0, pix_coord + ivec2(1, 0), vec4(depth1));
	imageStore(dest_view_z0, pix_coord + ivec2(0, 1), vec4(depth2));
	imageStore(dest_view_z0, pix_coord + ivec2(1, 1), vec4(depth3));

	float dm1 = depth_mip_filter(depth0, depth1, depth2, depth3);
	imageStore(dest_view_z1, ivec2(base_coord), vec4(dm1));
	g_scratch_depths[group_thread.x][group_thread.y] = dm1;

	memoryBarrierShared();
	barrier();

	if (all(equal(group_thread % uvec2(2), uvec2(0)))) {
		float in_tl = g_scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = g_scratch_depths[group_thread.x + 1][group_thread.y + 0];
		float in_bl = g_scratch_depths[group_thread.x + 0][group_thread.y + 1];
		float in_br = g_scratch_depths[group_thread.x + 1][group_thread.y + 1];
		float dm2 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		imageStore(dest_view_z2, ivec2(base_coord / 2u), vec4(dm2));
		g_scratch_depths[group_thread.x][group_thread.y] = dm2;
	}

	memoryBarrierShared();
	barrier();

	if (all(equal(group_thread % uvec2(4), uvec2(0)))) {
		float in_tl = g_scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = g_scratch_depths[group_thread.x + 2][group_thread.y + 0];
		float in_bl = g_scratch_depths[group_thread.x + 0][group_thread.y + 2];
		float in_br = g_scratch_depths[group_thread.x + 2][group_thread.y + 2];
		float dm3 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		imageStore(dest_view_z3, ivec2(base_coord / 4u), vec4(dm3));
		g_scratch_depths[group_thread.x][group_thread.y] = dm3;
	}

	memoryBarrierShared();
	barrier();

	if (all(equal(group_thread % uvec2(8), uvec2(0)))) {
		float in_tl = g_scratch_depths[group_thread.x + 0][group_thread.y + 0];
		float in_tr = g_scratch_depths[group_thread.x + 4][group_thread.y + 0];
		float in_bl = g_scratch_depths[group_thread.x + 0][group_thread.y + 4];
		float in_br = g_scratch_depths[group_thread.x + 4][group_thread.y + 4];
		float dm4 = depth_mip_filter(in_tl, in_tr, in_bl, in_br);
		imageStore(dest_view_z4, ivec2(base_coord / 8u), vec4(dm4));
	}
}

#endif // MODE_PREFILTER

#ifdef MODE_MAIN

// XeGTAO_FastACos(): Eberly's 1st order polynomial approximation, max error 0.0067 rad.
float fast_acos(float p_x) {
	float x = abs(p_x);
	float res = -0.156583 * x + XE_GTAO_PI_HALF;
	res *= sqrt(1.0 - x);
	return (p_x >= 0.0) ? res : XE_GTAO_PI - res;
}

vec3 compute_view_space_position(vec2 p_screen_pos, float p_view_z) {
	if (bool(params.is_orthogonal)) {
		return vec3(params.ndc_to_view_mul * p_screen_pos + params.ndc_to_view_add, p_view_z);
	}
	return vec3((params.ndc_to_view_mul * p_screen_pos + params.ndc_to_view_add) * p_view_z, p_view_z);
}

// Point sampling with clamp to edge, as XeGTAO's point clamp sampler. The view-z texture is padded to a multiple of
// 16 texels (MODE_PREFILTER's tile) so every mip exists; the clamp keeps reads inside the real image.
float load_view_z(ivec2 p_coord, int p_mip) {
	ivec2 coord = clamp(p_coord, ivec2(0), params.size - 1) >> p_mip;
	return texelFetch(src_view_z, coord, p_mip).x;
}

// Same decoding as ssao.glsl's load_normal(): view space, +z away from the camera.
vec3 load_normal(ivec2 p_pos) {
	vec3 encoded_normal = normalize(imageLoad(source_normal, p_pos).xyz * 2.0 - 1.0);
	encoded_normal.z = -encoded_normal.z;
	return encoded_normal;
}

// XeGTAO_CalculateEdges(): 1 means no depth discontinuity towards that neighbor, 0 an edge.
vec4 calculate_edges(float p_center_z, float p_left_z, float p_right_z, float p_top_z, float p_bottom_z) {
	vec4 edges_lrtb = vec4(p_left_z, p_right_z, p_top_z, p_bottom_z) - p_center_z;
	float slope_lr = (edges_lrtb.y - edges_lrtb.x) * 0.5;
	float slope_tb = (edges_lrtb.w - edges_lrtb.z) * 0.5;
	vec4 edges_lrtb_slope_adjusted = edges_lrtb + vec4(slope_lr, -slope_lr, slope_tb, -slope_tb);
	edges_lrtb = min(abs(edges_lrtb), abs(edges_lrtb_slope_adjusted));
	return clamp(1.25 - edges_lrtb / (p_center_z * 0.011), 0.0, 1.0);
}

// XeGTAO_PackEdges(): 2 bits per edge.
float pack_edges(vec4 p_edges_lrtb) {
	p_edges_lrtb = round(clamp(p_edges_lrtb, 0.0, 1.0) * 2.9);
	return dot(p_edges_lrtb, vec4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// XeGTAO's spatio-temporal noise: a 64x64 Hilbert curve index walked with the R2 low-discrepancy sequence
// (Roberts 2018, "The Unreasonable Effectiveness of Quasirandom Sequences"). With TAA the index advances every frame
// (288 per frame is XeGTAO's choice); without it it stays at 0, so the image is still.
uint hilbert_index(uint p_x, uint p_y) {
	uint index = 0u;
	uint pos_x = p_x & 63u;
	uint pos_y = p_y & 63u;
	for (uint cur_level = 32u; cur_level > 0u; cur_level /= 2u) {
		uint region_x = (pos_x & cur_level) > 0u ? 1u : 0u;
		uint region_y = (pos_y & cur_level) > 0u ? 1u : 0u;
		index += cur_level * cur_level * ((3u * region_x) ^ region_y);
		if (region_y == 0u) {
			if (region_x == 1u) {
				pos_x = 63u - pos_x;
				pos_y = 63u - pos_y;
			}
			uint temp = pos_x;
			pos_x = pos_y;
			pos_y = temp;
		}
	}
	return index;
}

vec2 spatio_temporal_noise(uvec2 p_pix_coord, uint p_temporal_index) {
	uint index = hilbert_index(p_pix_coord.x, p_pix_coord.y);
	index += 288u * (p_temporal_index % 64u);
	return fract(0.5 + float(index) * vec2(0.75487766624669276005, 0.5698402909980532659114));
}

void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.size))) {
		return;
	}

	ivec2 full_coord = pix_coord * params.scale;
	vec2 normalized_screen_pos = (vec2(full_coord) + 0.5) * params.full_pixel_size;
	// One working pixel, in full resolution UV (half resolution texel h holds full resolution texel 2h).
	vec2 working_pixel_uv = params.full_pixel_size * float(params.scale);

	float view_z = load_view_z(pix_coord, 0);
	float left_z = load_view_z(pix_coord + ivec2(-1, 0), 0);
	float right_z = load_view_z(pix_coord + ivec2(1, 0), 0);
	float top_z = load_view_z(pix_coord + ivec2(0, -1), 0);
	float bottom_z = load_view_z(pix_coord + ivec2(0, 1), 0);
	vec4 edges_lrtb = calculate_edges(view_z, left_z, right_z, top_z, bottom_z);

	// Move the center slightly towards the camera against FP16 depth imprecision (XeGTAO's value for FP16 depth).
	view_z *= 0.99920;

	vec3 pix_center_pos = compute_view_space_position(normalized_screen_pos, view_z);
	vec3 view_vec = bool(params.is_orthogonal) ? vec3(0.0, 0.0, -1.0) : normalize(-pix_center_pos);
	// The background (sky): nothing drawn there, so no occlusion. Nothing reads AO there, but a clean 1 keeps the SSAO
	// debug view and the denoiser's neighborhoods free of garbage. Both tests: the depth prepass clears
	// normal_roughness to 0 where nothing was drawn, and the depth test also catches a non-finite depth (the
	// negated comparison is true for NaN).
	if (!(view_z < params.sky_view_z) || imageLoad(source_normal, full_coord).xyz == vec3(0.0)) {
		// All edges: the denoiser keeps the background and the geometry from averaging into each other.
		imageStore(dest_ao, pix_coord, vec4(1.0, pack_edges(vec4(0.0)), 0.0, 0.0));
		return;
	}
	vec3 view_space_normal = load_normal(full_coord);

	float effect_radius = params.effect_radius;
	float falloff_mul = params.falloff_mul;
	float falloff_add = params.falloff_add;

	vec2 local_noise = spatio_temporal_noise(uvec2(pix_coord), params.noise_index);
	float noise_slice = local_noise.x;
	float noise_sample = local_noise.y;

	// Fade out for small screen radii, where there are too few pixels to sample.
	const float pixel_too_close_threshold = 1.3;
	vec2 pixel_dir_rb_view_space_size_at_center_z = bool(params.is_orthogonal) ? params.ndc_to_view_mul_x_pixel_size : view_z * params.ndc_to_view_mul_x_pixel_size;
	float screen_space_radius = effect_radius / pixel_dir_rb_view_space_size_at_center_z.x;

	float visibility = 0.0;
	if (screen_space_radius < pixel_too_close_threshold) {
		visibility = 1.0;
	} else {
		visibility += clamp((10.0 - screen_space_radius) / 100.0, 0.0, 1.0) * 0.5;
		float min_s = pixel_too_close_threshold / screen_space_radius;

		for (int slice_i = 0; slice_i < params.slice_count; slice_i++) {
			float slice_k = (float(slice_i) + noise_slice) / float(params.slice_count);
			float phi = slice_k * XE_GTAO_PI;
			float cos_phi = cos(phi);
			float sin_phi = sin(phi);
			// Screen space y points down, view space y up.
			vec2 omega = vec2(cos_phi, -sin_phi) * screen_space_radius;

			vec3 direction_vec = vec3(cos_phi, sin_phi, 0.0);
			vec3 ortho_direction_vec = direction_vec - (dot(direction_vec, view_vec) * view_vec);
			vec3 axis_vec = normalize(cross(ortho_direction_vec, view_vec));
			vec3 projected_normal_vec = view_space_normal - axis_vec * dot(view_space_normal, axis_vec);

			float sign_norm = sign(dot(ortho_direction_vec, projected_normal_vec));
			float projected_normal_vec_length = length(projected_normal_vec);
			float cos_norm = clamp(dot(projected_normal_vec, view_vec) / projected_normal_vec_length, 0.0, 1.0);
			float n = sign_norm * fast_acos(cos_norm);

			float low_horizon_cos0 = cos(n + XE_GTAO_PI_HALF);
			float low_horizon_cos1 = cos(n - XE_GTAO_PI_HALF);
			float horizon_cos0 = low_horizon_cos0;
			float horizon_cos1 = low_horizon_cos1;

			for (int step_i = 0; step_i < params.steps_per_slice; step_i++) {
				// R1 sequence (golden ratio) per step, offset by the per pixel noise.
				float step_base_noise = float(slice_i + step_i * params.steps_per_slice) * 0.6180339887498948482;
				float step_noise = fract(noise_sample + step_base_noise);

				float s = (float(step_i) + step_noise) / float(params.steps_per_slice);
				s = pow(s, params.sample_distribution_power);
				s += min_s;

				vec2 sample_offset = s * omega; // Working pixels.
				float sample_offset_length = length(sample_offset);
				float mip_level = clamp(log2(sample_offset_length) - params.depth_mip_sampling_offset, 0.0, XE_GTAO_DEPTH_MIP_LEVELS - 1.0);
				// XeGTAO samples with a point mip filter, which takes the nearest mip.
				int mip = int(mip_level + 0.5);

				// Snap to pixel centers, so the reconstructed position matches the depth that is read.
				ivec2 sample_offset_px = ivec2(round(sample_offset));
				vec2 sample_offset_uv = vec2(sample_offset_px) * working_pixel_uv;

				vec2 sample_screen_pos0 = normalized_screen_pos + sample_offset_uv;
				float sz0 = load_view_z(pix_coord + sample_offset_px, mip);
				vec3 sample_pos0 = compute_view_space_position(sample_screen_pos0, sz0);

				vec2 sample_screen_pos1 = normalized_screen_pos - sample_offset_uv;
				float sz1 = load_view_z(pix_coord - sample_offset_px, mip);
				vec3 sample_pos1 = compute_view_space_position(sample_screen_pos1, sz1);

				vec3 sample_delta0 = sample_pos0 - pix_center_pos;
				vec3 sample_delta1 = sample_pos1 - pix_center_pos;
				float sample_dist0 = length(sample_delta0);
				float sample_dist1 = length(sample_delta1);

				vec3 sample_horizon_vec0 = sample_delta0 / max(sample_dist0, 1e-6);
				vec3 sample_horizon_vec1 = sample_delta1 / max(sample_dist1, 1e-6);

				// Distance falloff: samples past the radius fade towards the "no occluder" horizon.
				float weight0 = clamp(sample_dist0 * falloff_mul + falloff_add, 0.0, 1.0);
				float weight1 = clamp(sample_dist1 * falloff_mul + falloff_add, 0.0, 1.0);

				float shc0 = dot(sample_horizon_vec0, view_vec);
				float shc1 = dot(sample_horizon_vec1, view_vec);
				shc0 = mix(low_horizon_cos0, shc0, weight0);
				shc1 = mix(low_horizon_cos1, shc1, weight1);

				horizon_cos0 = max(horizon_cos0, shc0);
				horizon_cos1 = max(horizon_cos1, shc1);
			}

			// XeGTAO: a small fudge towards 1 that reduces over-darkening on slopes.
			projected_normal_vec_length = mix(projected_normal_vec_length, 1.0, 0.05);

			float h0 = -fast_acos(horizon_cos1);
			float h1 = fast_acos(horizon_cos0);

			// The cosine weighted visibility over the slice, between the two horizons (Jimenez et al. 2016, eq. 10).
			float iarc0 = (cos_norm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
			float iarc1 = (cos_norm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
			float local_visibility = projected_normal_vec_length * (iarc0 + iarc1);
			visibility += local_visibility;
		}
		visibility /= float(params.slice_count);
		visibility = pow(max(visibility, 0.0), params.final_value_power);
		visibility = max(0.03, visibility); // XeGTAO: disallow total occlusion.
	}

	// The Environment's controls, as ASSAO applies them (ssao.glsl): intensity scales the occlusion, which is clamped
	// and faded out with distance.
	float obscurance = clamp((1.0 - visibility) * params.intensity, 0.0, 0.98);
	float fade_out = clamp(view_z * params.fade_out_mul + params.fade_out_add, 0.0, 1.0);
	obscurance *= fade_out;

	imageStore(dest_ao, pix_coord, vec4(1.0 - obscurance, pack_edges(edges_lrtb), 0.0, 0.0));
}

#endif // MODE_MAIN

#ifdef MODE_DENOISE

vec4 unpack_edges(float p_packed) {
	uint packed_val = uint(p_packed * 255.5);
	vec4 edges_lrtb;
	edges_lrtb.x = float((packed_val >> 6) & 0x03u) / 3.0;
	edges_lrtb.y = float((packed_val >> 4) & 0x03u) / 3.0;
	edges_lrtb.z = float((packed_val >> 2) & 0x03u) / 3.0;
	edges_lrtb.w = float((packed_val >> 0) & 0x03u) / 3.0;
	return clamp(edges_lrtb + params.edge_bias, 0.0, 1.0);
}

// XeGTAO_Denoise(): a 3x3 edge-aware blur, one pixel per thread.
void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.size))) {
		return;
	}

	vec2 center = load_ao(pix_coord, params.size);
	vec2 left = load_ao(pix_coord + ivec2(-1, 0), params.size);
	vec2 right = load_ao(pix_coord + ivec2(1, 0), params.size);
	vec2 top = load_ao(pix_coord + ivec2(0, -1), params.size);
	vec2 bottom = load_ao(pix_coord + ivec2(0, 1), params.size);
	float ao_tl = load_ao(pix_coord + ivec2(-1, -1), params.size).x;
	float ao_tr = load_ao(pix_coord + ivec2(1, -1), params.size).x;
	float ao_bl = load_ao(pix_coord + ivec2(-1, 1), params.size).x;
	float ao_br = load_ao(pix_coord + ivec2(1, 1), params.size).x;

	vec4 edges_c_lrtb = unpack_edges(center.y);
	vec4 edges_l_lrtb = unpack_edges(left.y);
	vec4 edges_r_lrtb = unpack_edges(right.y);
	vec4 edges_t_lrtb = unpack_edges(top.y);
	vec4 edges_b_lrtb = unpack_edges(bottom.y);

	// Edges are made symmetric: a neighbor only contributes if it also sees no edge towards this pixel.
	edges_c_lrtb *= vec4(edges_l_lrtb.y, edges_r_lrtb.x, edges_t_lrtb.w, edges_b_lrtb.z);

	// Let a little AO leak in from the neighbors where 3 or 4 edges surround the pixel, which reduces both spatial and
	// temporal aliasing (XeGTAO's constants).
	const float leak_threshold = 2.5;
	const float leak_strength = 0.5;
	float edginess = (clamp(4.0 - leak_threshold - dot(edges_c_lrtb, vec4(1.0)), 0.0, 1.0) / (4.0 - leak_threshold)) * leak_strength;
	edges_c_lrtb = clamp(edges_c_lrtb + edginess, 0.0, 1.0);

	const float diag_weight = 0.85 * 0.5;
	float weight_tl = diag_weight * (edges_c_lrtb.x * edges_l_lrtb.z + edges_c_lrtb.z * edges_t_lrtb.x);
	float weight_tr = diag_weight * (edges_c_lrtb.z * edges_t_lrtb.y + edges_c_lrtb.y * edges_r_lrtb.z);
	float weight_bl = diag_weight * (edges_c_lrtb.w * edges_b_lrtb.x + edges_c_lrtb.x * edges_l_lrtb.w);
	float weight_br = diag_weight * (edges_c_lrtb.y * edges_r_lrtb.w + edges_c_lrtb.w * edges_b_lrtb.y);

	float sum = center.x * params.blur_beta;
	sum += left.x * edges_c_lrtb.x + right.x * edges_c_lrtb.y + top.x * edges_c_lrtb.z + bottom.x * edges_c_lrtb.w;
	sum += ao_tl * weight_tl + ao_tr * weight_tr + ao_bl * weight_bl + ao_br * weight_br;
	float weight = params.blur_beta + dot(edges_c_lrtb, vec4(1.0)) + weight_tl + weight_tr + weight_bl + weight_br;

	float ao = sum / weight;

#ifdef FINAL_APPLY
	imageStore(dest_final, pix_coord, vec4(ao));
#else
	imageStore(dest_ao, pix_coord, vec4(ao, center.y, 0.0, 0.0));
#endif
}

#endif // MODE_DENOISE

#ifdef MODE_UPSAMPLE

// Half resolution to full: bilinear weights times a depth similarity weight, so occlusion does not bleed across
// depth discontinuities. Working texel h holds the full resolution texel 2h (see MODE_PREFILTER).
void main() {
	ivec2 pix_coord = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pix_coord, params.size))) {
		return;
	}

	float full_z = load_view_z_full(pix_coord);
	ivec2 base = pix_coord / 2;
	vec2 f = vec2(pix_coord - base * 2) * 0.5;

	float sum = 0.0;
	float weight_sum = 0.0;
	for (int y = 0; y < 2; y++) {
		for (int x = 0; x < 2; x++) {
			ivec2 tap = clamp(base + ivec2(x, y), ivec2(0), params.src_size - 1);
			float bilinear = (x == 0 ? 1.0 - f.x : f.x) * (y == 0 ? 1.0 - f.y : f.y);
			float tap_z = texelFetch(src_view_z, tap, 0).x;
			// 2% relative depth difference halves the weight; a look choice that keeps edges clean at half resolution.
			float depth_weight = 1.0 / (1.0 + abs(tap_z - full_z) / (0.02 * max(full_z, 1e-4)));
			float w = bilinear * depth_weight + 1e-5;
			sum += load_ao(tap, params.src_size).x * w;
			weight_sum += w;
		}
	}

	imageStore(dest_final, pix_coord, vec4(sum / weight_sum));
}

#endif // MODE_UPSAMPLE
