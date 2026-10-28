// CameraAttributes local exposure in the Forward+ tonemap pass (camera_attributes_storage.cpp, "Local exposure", has
// the model; camera_local_exposure.glsl builds the grid). Unreal Engine's Bilateral local exposure: the frame's log
// luminance is split into a base layer, which the bilateral grid and a blurred luminance give, and the detail on top
// of it; the base layer's contrast around middle gray is scaled down, the detail kept, and the pixel is exposed by the
// difference.
//
// CameraEffectsUBO::header[3] holds these bits (RendererCameraAttributes::LocalExposureBits), local_exposure[2] the
// parameters:
// - local_exposure[0]: highlight contrast, shadow contrast, detail strength, blurred luminance blend.
// - local_exposure[1]: x middle gray (log2, relative to 0.18), yz the grid's extent per unit of UV (the grid is whole
//   cells, so its last column reaches past the frame's edge), w unused.
//
// Before including, define CAMERA_LOCAL_EXPOSURE_SET and CAMERA_LOCAL_EXPOSURE_BINDING: where the grid is bound, a 3D
// texture of GRID_DEPTH + 1 slices of (sum of log2 luminance, count), the last slice the blurred luminance.

#define CAMERA_LOCAL_EXPOSURE_BILATERAL 1u

// Must match camera_local_exposure.glsl.
#define CAMERA_LOCAL_EXPOSURE_GRID_DEPTH 32.0
#define CAMERA_LOCAL_EXPOSURE_GRID_LOG2_MIN -16.0
#define CAMERA_LOCAL_EXPOSURE_MIDDLE_GRAY 0.18

layout(set = CAMERA_LOCAL_EXPOSURE_SET, binding = CAMERA_LOCAL_EXPOSURE_BINDING) uniform sampler3D camera_local_exposure_grid;

// The grid's sum over its count, or p_fallback where nothing landed near enough to count.
float camera_local_exposure_mean(vec2 p_sum_count, float p_fallback) {
	return p_sum_count.y > 1e-7 ? p_sum_count.x / p_sum_count.y : p_fallback;
}

// p_color is exposed scene light; p_uv where in the frame it came from (the grid was built from the source image).
vec3 camera_local_exposure_apply(vec3 p_color, vec2 p_uv) {
	vec4 contrast = camera_effects.local_exposure[0];
	vec4 frame = camera_effects.local_exposure[1];

	// Rec. ITU-R BT.709 relative luminance, log2, relative to middle gray.
	float luminance_log = log2(max(dot(p_color, vec3(0.2126, 0.7152, 0.0722)), 1e-10) / CAMERA_LOCAL_EXPOSURE_MIDDLE_GRAY);

	// Slicing: trilinear at the pixel's position and log luminance (Chen, Paris and Durand 2007), kept off the last
	// slice, which is the blurred luminance, read bilinearly at its texel center.
	const float depth = CAMERA_LOCAL_EXPOSURE_GRID_DEPTH + 1.0;
	vec2 grid_uv = p_uv * frame.yz;
	float grid_z = clamp(luminance_log - CAMERA_LOCAL_EXPOSURE_GRID_LOG2_MIN, 0.5, CAMERA_LOCAL_EXPOSURE_GRID_DEPTH - 0.5) / depth;
	float bilateral = camera_local_exposure_mean(textureLod(camera_local_exposure_grid, vec3(grid_uv, grid_z), 0.0).rg, luminance_log);
	float blurred = camera_local_exposure_mean(textureLod(camera_local_exposure_grid, vec3(grid_uv, (CAMERA_LOCAL_EXPOSURE_GRID_DEPTH + 0.5) / depth), 0.0).rg, luminance_log);
	float base = mix(bilateral, blurred, contrast.w);

	// Unreal's local exposure: the base layer's distance from middle gray scaled by the highlight or shadow contrast,
	// the detail (the pixel's distance from its base) by the detail strength. Continuous where the two contrasts meet,
	// since the scaled distance is zero there.
	float middle_gray = frame.x;
	float base_offset = base - middle_gray;
	float target_log = middle_gray + base_offset * (base_offset > 0.0 ? contrast.x : contrast.y) + (luminance_log - base) * contrast.z;
	return p_color * exp2(target_log - luminance_log);
}
