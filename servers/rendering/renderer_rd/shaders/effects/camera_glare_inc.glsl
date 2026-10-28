// Glare: light the lens, a filter in front of it or the film sends somewhere an ideal lens would not. The Environment's
// lens flare, veiling glare and diffusion filter and the CameraAttributes' starburst and halation, each a point spread
// function convolved with the scene-linear frame. Include after camera_effects_inc.glsl, and define
// CAMERA_GLARE_FETCH(m_uv, m_lod) to read the glare chain (a mip chain of the whole frame, see
// _render_camera_glare_chain()) in scene-linear units at a UV and lod, CAMERA_GLARE_FETCH_GRAD(m_uv, m_dx, m_dy) to
// read it with explicit derivatives through its anisotropic sampler, and CAMERA_GLARE_SIZE(m_lod) as the size of a lod
// in texels (ivec2).
//
// The parameters are in the glare region of the CameraEffects buffer, worked out per pass on the CPU
// (RendererCameraAttributes::_fill_effects_glare(), which also documents the models and their sources):
// - header.y: CAMERA_GLARE_* bits below; 0 = no glare.
// - glare[0]: lens flare: x = light per ghost, y = threshold (exposed units), z = ghost count, w = halo's light.
// - glare[1]: x = flare's red/blue size difference, y = frame aspect (width / height), zw = chain texel size in UV.
// - glare[2]: x = veiling glare fraction, y = diffusion fraction, z = the chain's last lod, w = the sigma of diffusion's
//   first octave (frame heights).
// - glare[3]: starburst: x = line density coefficient (per frame height), y = first tap distance (frame heights),
//   z = first spike's angle (radians, counterclockwise from +x), w = angle between spike lines.
// - glare[4]: x = starburst lines (2 spikes each), y = anamorphic squeeze, z = halation fraction, w = halation's lod
//   (camera_glare_lod()).
// - glare[5]: the chain's measured blur (lod L blurs to a Gaussian of variance A + B 4^L lod 0 texels squared): x = lod 0
//   texels per frame height over sqrt(B), z = A / B; y = starburst length (frame heights); w = 1 for the raster chain
//   (Mobile), whose levels are not where their UVs say (camera_glare_fetch_level()).

#define CAMERA_GLARE_LENS_FLARE 1u
#define CAMERA_GLARE_VEILING 2u
#define CAMERA_GLARE_DIFFUSION 4u
#define CAMERA_GLARE_DIFFUSION_BLACK 8u
#define CAMERA_GLARE_STARBURST 16u
#define CAMERA_GLARE_HALATION 32u

// The blur of p_sigma frame heights as a lod for camera_glare_fetch(): floor() is the lod whose blur is just below it,
// fract() the weight of the next one, so that the blend's best-fitting Gaussian has that sigma. The same function as
// _glare_lod_for_sigma() in camera_attributes_storage.cpp, which documents it; keep the two in step. Nothing is
// sharper than lod 0.
float camera_glare_lod(float p_sigma) {
	float s = p_sigma * camera_effects.glare[5].x;
	float s2 = s * s;
	float a = camera_effects.glare[5].z;
	float v = s2 - a;
	if (v <= 1.0) {
		return 0.0;
	}
	float max_lod = camera_effects.glare[2].z;
	float lod = floor(0.5 * log2(v));
	if (lod >= max_lod) {
		return max_lod;
	}
	float lower = 1.0 + (a + exp2(2.0 * lod)) / s2;
	float upper = 1.0 + (a + exp2(2.0 * lod + 2.0)) / s2;
	lower = 1.0 / (lower * lower);
	upper = 1.0 / (upper * upper);
	return min(lod + clamp((0.25 - lower) / (upper - lower), 0.0, 1.0), max_lod);
}

// The glare chain at an integer lod, read where the frame's p_uv is. The raster chain's downsample takes texel j of a
// level from coordinate 2 j + 1 of the one above (blur_raster.glsl), not from where its UV says, so a level of odd size
// drifts: texel coordinate x of lod L holds the frame's lod 0 coordinate 2^L x, up to a few percent of the frame off
// at 1080p (a halo 31 pixels below its source at lod 4, measured). Forward+'s passes read at their own UVs and need
// nothing.
vec3 camera_glare_fetch_level(vec2 p_uv, float p_lod) {
	vec2 uv = p_uv;
	if (camera_effects.glare[5].w > 0.0) {
		uv *= vec2(CAMERA_GLARE_SIZE(0)) / (exp2(p_lod) * vec2(CAMERA_GLARE_SIZE(int(p_lod))));
	}
	return CAMERA_GLARE_FETCH(uv, p_lod);
}

// The glare chain at a lod from camera_glare_lod(): the two integer lods, blended here rather than by the sampler,
// whose blend between mips is not linear on every GPU (NVIDIA weighs the upper one by min(2 t, 1) on this anisotropic
// sampler, measured), so the size is the same everywhere.
vec3 camera_glare_fetch(vec2 p_uv, float p_lod) {
	float lod = floor(p_lod);
	vec3 lower = camera_glare_fetch_level(p_uv, lod);
	float t = p_lod - lod;
	return t > 0.0 ? mix(lower, camera_glare_fetch_level(p_uv, lod + 1.0), t) : lower;
}

// Ghosts mirrored through the frame's center and a halo around it (Kawase 2004, Chapman 2013): ghost i at sensor
// position p shows the source at p / k_i, scaled by k_i (so its light spreads over k_i^2 the area) and defocused. The
// magnifications, tints (coating reflections) and blurs are look choices.
vec3 camera_glare_lens_flare(vec2 p_uv, float p_exposure) {
	const float magnification[8] = float[8](-0.55, -1.1, -1.7, 0.45, -0.3, 1.35, -2.3, 0.75);
	const vec3 tint[8] = vec3[8](vec3(1.0, 0.82, 0.55), vec3(0.55, 0.8, 1.0), vec3(0.65, 1.0, 0.65), vec3(1.0, 0.6, 0.9), vec3(1.0, 1.0, 0.8), vec3(0.6, 0.7, 1.0), vec3(1.0, 0.72, 0.5), vec3(0.8, 1.0, 0.9));
	vec4 g0 = camera_effects.glare[0];
	float aspect = camera_effects.glare[1].y;
	float chroma = camera_effects.glare[1].x;
	vec2 p = p_uv - 0.5;
	vec3 flare = vec3(0.0);
	int count = int(g0.z);
	for (int i = 0; i < 8; i++) {
		if (i >= count) {
			break;
		}
		float k = magnification[i];
		float lod = camera_glare_lod(0.004 + 0.008 * abs(k));
		vec3 ghost;
		vec2 r_uv = 0.5 + p / (k * (1.0 + chroma));
		vec2 g_uv = 0.5 + p / k;
		vec2 b_uv = 0.5 + p / (k * (1.0 - chroma));
		ghost.r = camera_glare_fetch(r_uv, lod).r;
		ghost.g = camera_glare_fetch(g_uv, lod).g;
		ghost.b = camera_glare_fetch(b_uv, lod).b;
		ghost = max(ghost * p_exposure - g0.y, vec3(0.0));
		// Only sources inside the frame make ghosts; fade at its border rather than clamping its edge outward.
		vec2 edge = abs(g_uv - 0.5);
		float window = (1.0 - smoothstep(0.42, 0.5, edge.x)) * (1.0 - smoothstep(0.42, 0.5, edge.y));
		flare += ghost * tint[i] * (window / (k * k));
	}
	flare *= g0.x;

	// Halo: a ring 0.3 frame heights from the source's mirror around the center, from the curved front element.
	if (g0.w > 0.0) {
		vec2 radial = p * vec2(aspect, 1.0);
		float len = length(radial);
		if (len > 1e-4) {
			vec2 s_uv = p_uv - (radial / len) * 0.3 * vec2(1.0 / aspect, 1.0);
			vec2 edge = abs(s_uv - 0.5);
			float window = (1.0 - smoothstep(0.42, 0.5, edge.x)) * (1.0 - smoothstep(0.42, 0.5, edge.y));
			vec3 halo = max(camera_glare_fetch(s_uv, camera_glare_lod(0.01)) * p_exposure - g0.y, vec3(0.0));
			flare += halo * vec3(0.9, 0.95, 1.0) * (g0.w * window);
		}
	}
	return flare;
}

// Diffraction spikes: a line density of C / x^2 of the source's light per unit length at distance x along each edge's
// normal, gathered from both sides of every spike line. The taps are spaced geometrically; each is an anisotropic read
// whose footprint is the stretch of the line it stands for (as long as the gap to the next tap) and 1/16 of that wide,
// so the sampler averages the light along the spike, and a point source draws a continuous, thin spike. The light in the
// spikes leaves the core: r_fraction is how much of a pixel's own light its spikes take, the same discrete sum the
// gather applies to a uniform field, so a flat field is unchanged. Past 90% (a strength far above the physical one)
// both are scaled down, so the spikes can never create light.
vec3 camera_glare_starburst(vec2 p_uv, out float r_fraction) {
	const int taps = 8;
	vec4 g3 = camera_effects.glare[3];
	float aspect = camera_effects.glare[1].y;
	float squeeze = camera_effects.glare[4].y;
	int lines = int(camera_effects.glare[4].x);
	float r0 = g3.y;
	float ratio = log2(camera_effects.glare[5].y / r0) / float(taps);
	float growth = exp2(ratio) - 1.0;
	vec3 sum = vec3(0.0);
	float weight = 0.0;
	for (int l = 0; l < 32; l++) {
		if (l >= lines) {
			break;
		}
		float angle = g3.z + g3.w * float(l);
		// An anamorphic lens stretches the far field horizontally by the squeeze.
		vec2 dir = normalize(vec2(cos(angle) * squeeze, sin(angle)));
		vec2 dir_uv = vec2(dir.x / aspect, -dir.y);
		vec2 across_uv = vec2(-dir.y / aspect, -dir.x);
		for (int j = 0; j < taps; j++) {
			float t = r0 * exp2(ratio * (float(j) + 0.5));
			float dt = t * growth;
			vec2 along = dir_uv * dt;
			vec2 across = across_uv * max(dt * (1.0 / 16.0), camera_effects.glare[1].w);
			vec3 light = CAMERA_GLARE_FETCH_GRAD(p_uv + dir_uv * t, along, across) + CAMERA_GLARE_FETCH_GRAD(p_uv - dir_uv * t, along, across);
			sum += light * (dt / (t * t));
			weight += 2.0 * dt / (t * t);
		}
	}
	float fraction = weight * g3.x;
	float scale = fraction > 0.9 ? 0.9 / fraction : 1.0;
	r_fraction = fraction * scale;
	return sum * (g3.x * scale);
}

// The glare added to (or redistributed in) the exposed scene color p_color, whose light comes from the source at p_uv.
// p_exposure turns the chain's scene-linear values into the same exposed units.
vec3 camera_glare_apply(vec3 p_color, vec2 p_uv, float p_exposure) {
	uint bits = camera_effects.header.y;
	vec3 color = p_color;
	if ((bits & CAMERA_GLARE_DIFFUSION) != 0u) {
		// (1 - s) I + s G, G the mean of four octaves of blur from the filter's base width.
		float base = camera_effects.glare[2].w;
		vec3 wide = camera_glare_fetch(p_uv, camera_glare_lod(base)) + camera_glare_fetch(p_uv, camera_glare_lod(2.0 * base)) + camera_glare_fetch(p_uv, camera_glare_lod(4.0 * base)) + camera_glare_fetch(p_uv, camera_glare_lod(8.0 * base));
		color = mix(color, wide * (0.25 * p_exposure), camera_effects.glare[2].y);
	}
	if ((bits & CAMERA_GLARE_VEILING) != 0u) {
		// The frame's mean light, spread evenly: a 4x4 grid of reads two lods above the last, whose texels each cover a
		// few percent of the frame. The one-texel lods are not used: the raster chain loses light in its smallest mips.
		vec3 mean = vec3(0.0);
		float lod = max(camera_effects.glare[2].z - 2.0, 0.0);
		for (int j = 0; j < 4; j++) {
			for (int i = 0; i < 4; i++) {
				mean += CAMERA_GLARE_FETCH((vec2(float(i), float(j)) + 0.5) * 0.25, lod);
			}
		}
		mean *= p_exposure / 16.0;
		color = mix(color, mean, camera_effects.glare[2].x);
	}
	if ((bits & CAMERA_GLARE_LENS_FLARE) != 0u) {
		color += camera_glare_lens_flare(p_uv, p_exposure);
	}
	if ((bits & CAMERA_GLARE_STARBURST) != 0u) {
		float fraction;
		vec3 spikes = camera_glare_starburst(p_uv, fraction) * p_exposure;
		color += spikes - fraction * color;
	}
	if ((bits & CAMERA_GLARE_HALATION) != 0u) {
		// Light moved from each point into a red-orange halo around it: red and some green, no blue (the yellow filter
		// layer stops blue before the base). Moved, so a flat field keeps its color.
		vec3 halo = camera_glare_fetch(p_uv, camera_effects.glare[4].w) * p_exposure;
		color += (camera_effects.glare[4].z * vec3(1.0, 0.3, 0.0)) * (halo - color);
	}
	return color;
}
