#[compute]

#version 450

#VERSION_DEFINES

// CameraAttributes local exposure (CameraLocalExposure, camera_local_exposure.cpp; the model is in
// camera_attributes_storage.cpp, "Local exposure"). Forward+ only. Builds the bilateral grid of the frame's log
// luminance the tonemap pass slices (camera_local_exposure_inc.glsl):
// - MODE_BUILD: one workgroup per grid cell column. Every pixel of the cell, exposed as the tonemapper exposes it, adds
//   its log2 luminance and a count of 1 to the range bin it falls in (Chen, Paris and Durand 2007, "Real-time
//   edge-aware image processing with the bilateral grid", grid creation), and to slice GRID_DEPTH, which keeps the
//   whole column for the blurred luminance. Sums go through workgroup atomics in fixed point (WGSL has no float
//   atomics), 1/4096 of a bin.
// - MODE_BLUR: the range slices get the grid's Gaussian, a binomial [1 4 6 4 1] / 16 along x, y and z (a sigma of one
//   cell; Paris and Durand 2006, "A fast approximation of the bilateral filter using a signal processing approach",
//   §4); slice GRID_DEPTH gets a 2D Gaussian of blur_sigma cells, the blurred luminance. Both are normalized
//   convolutions: cells outside the grid count as empty, and the slicing divides the blurred sum by the blurred count.

#include "finite_inc.glsl"

#ifdef MODE_BUILD
#define BUILD_SIZE 16
layout(local_size_x = BUILD_SIZE, local_size_y = BUILD_SIZE, local_size_z = 1) in;
#else
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#endif

// Must match camera_local_exposure_inc.glsl and RendererCameraAttributes::LOCAL_EXPOSURE_GRID_*.
#define GRID_DEPTH 32
#define GRID_LOG2_MIN -16.0 // Bin 0 starts 16 EV below middle gray; bin 31 ends 16 EV above it.
#define MIDDLE_GRAY 0.18
#define FIXED_POINT 4096.0

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	ivec2 grid_size; // Cells across and down.

	float cell_size; // In source pixels.
	float exposure; // The tonemapper's exposure, without auto exposure.
	float auto_exposure_scale;
	float luminance_multiplier;

	float blur_sigma; // The blurred luminance's Gaussian, in cells.
	int blur_radius; // In cells.
	uint flags;
	float pad;
}
params;

#define FLAG_USE_AUTO_EXPOSURE 1u

#ifdef MODE_BUILD

layout(set = 0, binding = 0) uniform sampler2D source_color;
layout(set = 0, binding = 1) uniform sampler2D source_auto_exposure;
layout(rgba32f, set = 1, binding = 0) uniform restrict writeonly image3D dest_grid;

shared uint bin_count[GRID_DEPTH];
shared uint bin_sum[GRID_DEPTH]; // Of each pixel's position inside its bin, in 1/FIXED_POINT.

void main() {
	uint local_index = gl_LocalInvocationIndex;
	if (local_index < uint(GRID_DEPTH)) {
		bin_count[local_index] = 0u;
		bin_sum[local_index] = 0u;
	}
	barrier();

	// The cell's pixels: those whose centers fall in it.
	ivec2 cell = ivec2(gl_WorkGroupID.xy);
	ivec2 from = ivec2(ceil(vec2(cell) * params.cell_size - 0.5));
	ivec2 to = min(ivec2(ceil(vec2(cell + 1) * params.cell_size - 0.5)), params.source_size);

	float exposure = params.luminance_multiplier * params.exposure;
	if ((params.flags & FLAG_USE_AUTO_EXPOSURE) != 0u) {
		exposure *= 1.0 / (texelFetch(source_auto_exposure, ivec2(0, 0), 0).r * params.luminance_multiplier / params.auto_exposure_scale);
	}

	for (int y = from.y + int(gl_LocalInvocationID.y); y < to.y; y += BUILD_SIZE) {
		for (int x = from.x + int(gl_LocalInvocationID.x); x < to.x; x += BUILD_SIZE) {
			vec3 color = finite_or_zero3(texelFetch(source_color, ivec2(x, y), 0).rgb) * exposure;
			// Rec. ITU-R BT.709 relative luminance, as the tonemap pass computes it.
			float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
			float z = clamp(log2(max(luminance, 1e-10) / MIDDLE_GRAY) - GRID_LOG2_MIN, 0.0, float(GRID_DEPTH));
			uint bin = min(uint(z), uint(GRID_DEPTH - 1));
			atomicAdd(bin_count[bin], 1u);
			atomicAdd(bin_sum[bin], uint((z - float(bin)) * FIXED_POINT + 0.5));
		}
	}
	barrier();

	// Weights per cell row of pixels rather than per pixel keep a full cell's count near the cell size, well inside
	// the range of the half-float grid the blur writes; the slicing only ever divides sums by counts.
	float norm = 1.0 / params.cell_size;
	if (local_index < uint(GRID_DEPTH)) {
		float count = float(bin_count[local_index]);
		float sum = float(bin_sum[local_index]) / FIXED_POINT + count * (float(local_index) + GRID_LOG2_MIN);
		imageStore(dest_grid, ivec3(cell, int(local_index)), vec4(sum * norm, count * norm, 0.0, 0.0));
	} else if (local_index == uint(GRID_DEPTH)) {
		float count = 0.0;
		float sum = 0.0;
		for (int i = 0; i < GRID_DEPTH; i++) {
			float bin_n = float(bin_count[i]);
			count += bin_n;
			sum += float(bin_sum[i]) / FIXED_POINT + bin_n * (float(i) + GRID_LOG2_MIN);
		}
		imageStore(dest_grid, ivec3(cell, GRID_DEPTH), vec4(sum * norm, count * norm, 0.0, 0.0));
	}
}

#endif // MODE_BUILD

#ifdef MODE_BLUR

layout(rgba32f, set = 0, binding = 0) uniform restrict readonly image3D source_grid;
layout(rgba16f, set = 1, binding = 0) uniform restrict writeonly image3D dest_grid;

// The binomial [1 4 6 4 1], unnormalized.
float binomial_weight(int d) {
	return d == 0 ? 6.0 : (abs(d) == 1 ? 4.0 : 1.0);
}

void main() {
	ivec3 cell = ivec3(gl_GlobalInvocationID);
	if (cell.x >= params.grid_size.x || cell.y >= params.grid_size.y || cell.z > GRID_DEPTH) {
		return;
	}

	vec2 sum = vec2(0.0);
	if (cell.z < GRID_DEPTH) {
		for (int dz = -2; dz <= 2; dz++) {
			int z = cell.z + dz;
			if (z < 0 || z >= GRID_DEPTH) {
				continue;
			}
			for (int dy = -2; dy <= 2; dy++) {
				int y = cell.y + dy;
				if (y < 0 || y >= params.grid_size.y) {
					continue;
				}
				for (int dx = -2; dx <= 2; dx++) {
					int x = cell.x + dx;
					if (x < 0 || x >= params.grid_size.x) {
						continue;
					}
					sum += (binomial_weight(dx) * binomial_weight(dy) * binomial_weight(dz)) * imageLoad(source_grid, ivec3(x, y, z)).rg;
				}
			}
		}
		sum *= 1.0 / 4096.0; // 16^3.
	} else {
		float inv_two_sigma_sq = 1.0 / (2.0 * params.blur_sigma * params.blur_sigma);
		float total = 0.0;
		for (int dy = -params.blur_radius; dy <= params.blur_radius; dy++) {
			for (int dx = -params.blur_radius; dx <= params.blur_radius; dx++) {
				float w = exp(-float(dx * dx + dy * dy) * inv_two_sigma_sq);
				total += w;
				ivec2 p = cell.xy + ivec2(dx, dy);
				if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, params.grid_size))) {
					continue;
				}
				sum += w * imageLoad(source_grid, ivec3(p, GRID_DEPTH)).rg;
			}
		}
		sum *= 1.0 / total;
	}
	imageStore(dest_grid, cell, vec4(sum, 0.0, 0.0));
}

#endif // MODE_BLUR
