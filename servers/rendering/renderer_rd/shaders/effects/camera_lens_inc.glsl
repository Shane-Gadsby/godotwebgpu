// CameraAttributes lens optics: geometric distortion, lateral chromatic aberration, diffraction and vignetting, which
// act on the light before it reaches the sensor, so the tonemap pass applies them first: before white balance, grain
// and tonemapping. Include after camera_effects_inc.glsl. To resample the source, define
// CAMERA_LENS_FETCH(m_uv) to read the source color at a UV first (bilinear, clamped to the edge). The Compatibility
// renderer has a copy in drivers/gles3/shaders/effects/camera_lens_inc.glsl: keep the two in step.
//
// The parameters are in the lens region of the CameraEffects buffer, worked out per pass on the CPU
// (RendererCameraAttributes::_fill_effects_lens(), which also documents the models and their sources):
// - header.x: CAMERA_LENS_* bits below; 0 = no lens effect.
// - lens[0]: xy = half extent of the frame in frame units (the frame's corner is at |q| = 1), z = distortion crop
//   (output positions are scaled by it, < 1 zooms in), w = anamorphic squeeze.
// - lens[1]: Brown-Conrady k1, k2 for the x axis, then k1, k2 for the y axis (equal unless anamorphic).
// - lens[2]: xy = position scale for the red and blue channels (lateral color); green is 1. zw = Panini: the output
//   frame's half extent in the Panini plane (fit scale times the rendered frame's tangent half extent).
// - lens[3]: physical vignetting: x = strength (scales the loss in stops), y = (frame radius / focal length)^2,
//   z = pupil offset per frame unit, w = pupil radius relative to the lens barrel.
// - lens[4]: practical vignetting: x = stops of darkening at the corner, y = falloff exponent. zw = Panini: 1 / the
//   rendered frame's tangent half extent.
// - lens[5]: diffraction: xy = tap spacing in UV, zw = the Gaussian's sigma in texels (informational).
// - lens[6]: the physical lens: sensor width and height (mm), focal length (mm), f-number; zero without one.
// - lens[7]: xy = source texel size in UV; zw = Panini distance and vertical compression.

#define CAMERA_LENS_DISTORTION 1u
#define CAMERA_LENS_CHROMATIC_ABERRATION 2u
#define CAMERA_LENS_DIFFRACTION 4u
#define CAMERA_LENS_VIGNETTING_PHYSICAL 8u
#define CAMERA_LENS_VIGNETTING_PRACTICAL 16u
#define CAMERA_LENS_PANINI 32u
#define CAMERA_LENS_RESAMPLE (CAMERA_LENS_DISTORTION | CAMERA_LENS_CHROMATIC_ABERRATION | CAMERA_LENS_DIFFRACTION | CAMERA_LENS_PANINI)
#define CAMERA_LENS_VIGNETTING (CAMERA_LENS_VIGNETTING_PHYSICAL | CAMERA_LENS_VIGNETTING_PRACTICAL)

// The output pixel's position on the sensor, in frame units: the crop zooms into the frame.
vec2 camera_lens_sensor_position(vec2 p_uv) {
	return (p_uv * 2.0 - 1.0) * camera_effects.lens[0].xy * camera_effects.lens[0].z;
}

// Inverts the distortion: finds the undistorted position q whose distorted image is p_qd, by Newton's method on the
// per-axis Brown-Conrady model (x_d = x (1 + k1 r^2 + k2 r^4)). Starting from p_qd it converges in a few steps for the
// coefficients the settings allow, where a plain fixed-point iteration diverges on strong barrel distortion.
vec2 camera_lens_undistort(vec2 p_qd) {
	vec4 k = camera_effects.lens[1];
	vec2 q = p_qd;
	for (int i = 0; i < 5; i++) {
		float r2 = dot(q, q);
		vec2 f = 1.0 + vec2(k.x, k.z) * r2 + vec2(k.y, k.w) * r2 * r2;
		vec2 df = vec2(k.x, k.z) + 2.0 * vec2(k.y, k.w) * r2; // d f / d r^2
		vec2 residual = q * f - p_qd;
		// Jacobian of q * f(r^2): diagonal f + 2 q^2 f', off-diagonal 2 x y f'.
		float j00 = f.x + 2.0 * q.x * q.x * df.x;
		float j01 = 2.0 * q.x * q.y * df.x;
		float j10 = 2.0 * q.x * q.y * df.y;
		float j11 = f.y + 2.0 * q.y * q.y * df.y;
		float det = j00 * j11 - j01 * j10;
		if (abs(det) < 1e-6) {
			break;
		}
		q -= vec2(j11 * residual.x - j01 * residual.y, j00 * residual.y - j10 * residual.x) / det;
	}
	return q;
}

// Panini projection (servers/rendering/panini_projection.h has the model and the C++ that Camera3D uses: keep them in
// step): from a position p_n in the output frame (-1 to 1 per axis) to the position in the rendered rectilinear frame
// that shows the same direction. The Panini plane position X = S sin(phi) is inverted for the azimuth with
// k = X / (d + 1): phi = atan(k) + asin(k d / sqrt(1 + k^2)).
vec2 camera_lens_panini_source(vec2 p_n) {
	vec2 panini = p_n * camera_effects.lens[2].zw;
	float d = camera_effects.lens[7].z;
	float k = panini.x / (d + 1.0);
	float phi = atan(k) + asin(clamp(k * d * inversesqrt(1.0 + k * k), -1.0, 1.0));
	float cos_phi = cos(phi);
	float s = (d + 1.0) / (d + cos_phi);
	// The vertical coordinate is the rectilinear one scaled by mix(1, S cos(phi), vertical compression).
	vec2 rectilinear = vec2(tan(phi), panini.y / mix(1.0, s * cos_phi, camera_effects.lens[7].w));
	return rectilinear * camera_effects.lens[4].zw;
}

// Where the light that lands at sensor position p_qd comes from in the rendered image, for a channel whose image is
// magnified by 1 / p_scale (lateral color).
vec2 camera_lens_source_uv(vec2 p_qd, float p_scale, uint p_bits) {
	vec2 q = p_qd * p_scale;
	if ((p_bits & CAMERA_LENS_DISTORTION) != 0u) {
		q = camera_lens_undistort(q);
	}
	vec2 n = q / camera_effects.lens[0].xy;
	if ((p_bits & CAMERA_LENS_PANINI) != 0u) {
		n = camera_lens_panini_source(n);
	}
	return n * 0.5 + 0.5;
}

// Fraction of a pupil of radius p_radius, offset by p_offset, that passes a unit circle (the lens barrel): the area of
// the two circles' intersection over the pupil's area.
float camera_lens_pupil_transmission(float p_offset, float p_radius) {
	if (p_offset + p_radius <= 1.0) {
		return 1.0;
	}
	if (p_offset >= 1.0 + p_radius) {
		return 0.0;
	}
	float d = p_offset;
	float r = p_radius;
	float a = acos(clamp((d * d + r * r - 1.0) / (2.0 * d * r), -1.0, 1.0));
	float b = acos(clamp((d * d + 1.0 - r * r) / (2.0 * d), -1.0, 1.0));
	float kite = 0.5 * sqrt(max((-d + r + 1.0) * (d + r - 1.0) * (d - r + 1.0) * (d + r + 1.0), 0.0));
	return (r * r * a + b - kite) / (3.14159265359 * r * r);
}

// The light the lens lets through to sensor position p_uv, relative to the center.
float camera_lens_vignetting(vec2 p_uv) {
	uint bits = camera_effects.header.x;
	vec2 q = camera_lens_sensor_position(p_uv);
	float r2 = dot(q, q);
	if ((bits & CAMERA_LENS_VIGNETTING_PRACTICAL) != 0u) {
		vec2 v = camera_effects.lens[4].xy;
		return exp2(-v.x * pow(r2, 0.5 * v.y));
	}
	vec4 v = camera_effects.lens[3];
	// Natural vignetting: cos^4 of the field angle, from tan^2 = r2 * (frame radius / focal length)^2.
	float cos2 = 1.0 / (1.0 + r2 * v.y);
	float transmission = cos2 * cos2;
	// Optical vignetting: the barrel clips the off-axis pupil.
	transmission *= camera_lens_pupil_transmission(sqrt(r2) * v.z, v.w);
	return pow(max(transmission, 1e-6), v.x);
}

#ifdef CAMERA_LENS_FETCH

// Diffraction: 5x5 bilinear taps of a Gaussian, exp(-k^2 / 2) normalized; the C++ solves the spacing for these exact
// weights (LENS_DIFFRACTION_WEIGHTS), so keep them in step.
vec4 camera_lens_fetch_diffracted(vec2 p_uv) {
	const float weights[3] = float[3](0.402619947, 0.244201342, 0.054488685);
	vec2 spacing = camera_effects.lens[5].xy;
	vec4 sum = vec4(0.0);
	for (int j = -2; j <= 2; j++) {
		for (int i = -2; i <= 2; i++) {
			sum += (weights[abs(i)] * weights[abs(j)]) * CAMERA_LENS_FETCH(p_uv + vec2(float(i), float(j)) * spacing);
		}
	}
	return sum;
}

vec4 camera_lens_fetch_channel(vec2 p_uv, uint p_bits) {
	// Outside the rendered frame (distortion without the crop) there is no image: black. A texel of slack keeps the
	// crop's own edge, which lands exactly on the frame's border, inside.
	vec2 outside = abs(p_uv - 0.5) - 0.5 - camera_effects.lens[7].xy;
	if (max(outside.x, outside.y) > 0.0) {
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	if ((p_bits & CAMERA_LENS_DIFFRACTION) != 0u) {
		return camera_lens_fetch_diffracted(p_uv);
	}
	return CAMERA_LENS_FETCH(p_uv);
}

// The source color seen through the lens at output position p_uv. r_uv is where the green channel came from, for
// reads that follow the image without needing the lens's full treatment (glow, FXAA's edge search).
vec4 camera_lens_sample(vec2 p_uv, out vec2 r_uv) {
	uint bits = camera_effects.header.x;
	vec2 qd = camera_lens_sensor_position(p_uv);
	r_uv = camera_lens_source_uv(qd, 1.0, bits);
	vec4 color = camera_lens_fetch_channel(r_uv, bits);
	if ((bits & CAMERA_LENS_CHROMATIC_ABERRATION) != 0u) {
		color.r = camera_lens_fetch_channel(camera_lens_source_uv(qd, camera_effects.lens[2].x, bits), bits).r;
		color.b = camera_lens_fetch_channel(camera_lens_source_uv(qd, camera_effects.lens[2].y, bits), bits).b;
	}
	return color;
}

#endif // CAMERA_LENS_FETCH
