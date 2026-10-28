layout(push_constant, std430) uniform Params {
	ivec2 size;
	float z_far;
	float z_near;

	bool orthogonal;
	float blur_size;
	float blur_scale;
	int blur_steps;

	bool blur_near_active;
	float blur_near_begin;
	float blur_near_end;
	bool blur_far_active;

	float blur_far_begin;
	float blur_far_end;
	bool second_pass;
	bool half_size;

	bool use_jitter;
	float jitter_seed;
	bool use_physical_near;
	bool use_physical_far;

	float blur_size_near;
	float blur_size_far;

	// CameraAttributes aperture, cat's eye and tilt-shift (BokehDOF::_setup_bokeh_shape()); half floats in pairs.
	uint shape_flags; // Bits 0-7: blade count (0 = circular); BOKEH_SHAPE_* above.
	uint blade_shape; // Blade curvature, rotation (radians).
	uint cats_eye; // Barrel offset per frame unit, barrel radius (in aperture radii).
	uint frame_half_extent;
	uint squeeze_focal_length; // Anamorphic squeeze, focal length (meters).
	uint view_ray_scale;
	uint view_ray_offset;
	float tilt_normal_x;
	float tilt_normal_y;
	uint pad;
}
params;

#define BOKEH_SHAPE_CUSTOM (1u << 8u)
#define BOKEH_SHAPE_CATS_EYE (1u << 9u)
#define BOKEH_SHAPE_TILT (1u << 10u)

// How far the bokeh of a defocused point at p_source_uv reaches in direction p_dir (unit, in pixels, from the point
// toward the pixel that gathers it), relative to its blur radius: 1 for the round default. The aperture is a polygon
// with its corners on the unit circle and its edges bowed out by the blade curvature; an anamorphic lens makes it an
// oval taller than wide by the squeeze; the cat's eye clips it with the barrel's circle. Near-field bokeh are the
// aperture point-reflected. RendererCameraAttributes::camera_attributes_get_bokeh_shape() has the models.
float bokeh_shape_extent(vec2 p_dir, vec2 p_source_uv, bool p_near) {
	// Pixel directions have y down; the aperture's angles run counterclockwise on screen.
	vec2 d = vec2(p_dir.x, -p_dir.y);
	if (p_near) {
		d = -d;
	}
	// The anamorphic oval: x in the round aperture's frame is the screen's x times the squeeze.
	float squeeze = unpackHalf2x16(params.squeeze_focal_length).x;
	vec2 a = vec2(d.x * squeeze, d.y);
	float scale = length(a);
	vec2 u = a / scale;

	float extent = 1.0;
	uint blades = params.shape_flags & 0xFFu;
	if (blades >= 3u) {
		vec2 shape = unpackHalf2x16(params.blade_shape); // Curvature, rotation.
		float sector = 6.28318530718 / float(blades);
		float angle = atan(u.y, u.x) - shape.y;
		float phi = angle - sector * floor(angle / sector) - 0.5 * sector;
		extent = mix(cos(0.5 * sector) / cos(phi), 1.0, shape.x);
	}
	if ((params.shape_flags & BOKEH_SHAPE_CATS_EYE) != 0u) {
		vec2 cats_eye = unpackHalf2x16(params.cats_eye); // Offset per frame unit, barrel radius.
		vec2 q = (p_source_uv * 2.0 - 1.0) * unpackHalf2x16(params.frame_half_extent);
		q.y = -q.y;
		// The barrel's circle sits toward the frame's center, clipping the side of the aperture that faces the edge.
		vec2 c = -cats_eye.x * q;
		// The ray t u leaves the circle |x - c| = R at t = u.c + sqrt((u.c)^2 - |c|^2 + R^2).
		float uc = dot(u, c);
		float disc = uc * uc - dot(c, c) + cats_eye.y * cats_eye.y;
		extent = min(extent, disc > 0.0 ? max(uc + sqrt(disc), 0.0) : 0.0);
	}
	return extent / scale;
}

// Tilt-shift: the distance at which the plane of sharp focus crosses the view ray through p_uv, along -Z, and the blur
// sizes rescaled for it. The circle of confusion at infinity is A f / (s - f) for focus distance s, so a pixel focused
// at s' has (s - f) / (s' - f) times the untilted lens's.
float bokeh_tilt_focus_distance(vec2 p_uv, float p_focus_distance, out float r_size_scale) {
	vec2 ndc = vec2(p_uv.x * 2.0 - 1.0, 1.0 - p_uv.y * 2.0);
	vec3 ray = vec3(ndc * unpackHalf2x16(params.view_ray_scale) + unpackHalf2x16(params.view_ray_offset), -1.0);
	vec3 n = vec3(params.tilt_normal_x, params.tilt_normal_y, 0.0);
	n.z = sqrt(max(1.0 - dot(n.xy, n.xy), 0.0));
	// The plane holds (0, 0, -s): n.(t ray) = n.(0, 0, -s) gives t = -s n.z / n.ray; t is the depth, as ray.z = -1.
	float denom = dot(n, ray);
	float focus = denom < -1e-6 ? -p_focus_distance * n.z / denom : 1e7;
	float f = unpackHalf2x16(params.squeeze_focal_length).y;
	r_size_scale = (p_focus_distance - f) / max(focus - f, 1e-4);
	return focus;
}

//used to work around downsampling filter
#define DEPTH_GAP 0.0

const float GOLDEN_ANGLE = 2.39996323;

//note: uniform pdf rand [0;1[
float hash12n(vec2 p) {
	p = fract(p * vec2(5.3987, 5.4421));
	p += dot(p.yx, p.xy + vec2(21.5351, 14.3137));
	return fract(p.x * p.y * 95.4307);
}

// The filters weight each sample with mix(color, sample, m), and NaN * 0 is still NaN, so one
// non-finite scene color would blank every pixel within the blur radius, in the bokeh shape. They
// read the scene color through finite_or_zero3() (finite_inc.glsl), which counts it as black.
#include "finite_inc.glsl"
