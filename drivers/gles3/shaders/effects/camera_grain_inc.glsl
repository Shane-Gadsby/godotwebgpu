// CameraAttributes grain for the Compatibility renderer, applied by the post pass (effects/post.glsl) to linear
// scene-referred color before tonemapping. A copy of servers/rendering/renderer_rd/shaders/effects/camera_grain_inc.glsl,
// which documents it: keep the two in step. The packed parameters come from the Tonemap UBO.
//
// The parameters arrive packed into one uint, because the Forward+ tonemap push constant is already at the
// 128 bytes every Vulkan device supports. 0 means no grain. Bits, low to high:
//   2  type: 1 color film, 2 black-and-white film, 3 digital sensor
//   7  log2 of the grain cell size in output pixels, -8..5
//   8  log2 of amplitude a, -16..4 (film: grain in stops at density 1; digital: shot noise at signal 1)
//   7  log2 of amplitude b, -16..4 (digital: read noise; unused by film)
//   8  frame seed

#define CAMERA_GRAIN_FILM 1u
#define CAMERA_GRAIN_FILM_MONOCHROME 2u
#define CAMERA_GRAIN_DIGITAL 3u

float camera_grain_decode(uint p_packed, uint p_shift, uint p_bits, float p_lo, float p_hi) {
	uint max_value = (1u << p_bits) - 1u;
	float t = float((p_packed >> p_shift) & max_value) / float(max_value);
	return exp2(mix(p_lo, p_hi, t));
}

// PCG4D (Jarzynski and Olano 2020, "Hash Functions for GPU Rendering").
uvec4 camera_grain_hash(uvec4 v) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	v ^= v >> 16u;
	v.x += v.y * v.w;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v.w += v.y * v.z;
	return v;
}

// Four unit Gaussians from four hashes: the sum of a hash's four bytes (Irwin-Hall), scaled to unit variance.
vec4 camera_grain_gaussian(uvec4 h) {
	uvec4 bytes_sum = (h & 0xFFu) + ((h >> 8u) & 0xFFu) + ((h >> 16u) & 0xFFu) + (h >> 24u);
	// Four uniform bytes: mean 510, variance 4 * (256^2 - 1) / 12 = 21845.
	return (vec4(bytes_sum) - 510.0) * (1.0 / 147.80054);
}

// Unit-variance noise, one value per grain: white per pixel when the grain is a pixel or smaller, and cellular
// when it is larger, each pixel taking the value of the nearest grain center on a lattice of grain-sized cells
// jittered anew every frame. Every pixel gets exactly unit variance, and each grain covers one cell's area on
// average, so Selwyn's law holds at every size. xyz are per channel, w is shared.
vec4 camera_grain_noise(vec2 p_frag_coord, float p_cell, uint p_seed, uint p_view) {
	uvec4 frame_hash = camera_grain_hash(uvec4(p_seed, p_view, 0x2545F491u, 0x9E3779B9u));
	ivec2 grain_id = ivec2(floor(p_frag_coord));
	if (p_cell > 1.0) {
		// The jitter fades in as cells grow from one pixel to two, so at one pixel the grains are the pixels and
		// the change of character is continuous. Up to 0.45 cells keeps the nearest center within the 3x3 search.
		float jitter = 0.9 * clamp(p_cell - 1.0, 0.0, 1.0);
		vec2 p = (p_frag_coord - 0.5) / p_cell;
		ivec2 base = ivec2(floor(p + 0.5));
		float best = 1e10;
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				ivec2 id = base + ivec2(x, y);
				uvec4 h = camera_grain_hash(uvec4(uvec2(id + 32768), frame_hash.x, p_view));
				vec2 d = p - (vec2(id) + (vec2(h.xy >> 16u) * (1.0 / 65536.0) - 0.5) * jitter);
				float dist = dot(d, d);
				if (dist < best) {
					best = dist;
					grain_id = id;
				}
			}
		}
	}
	return camera_grain_gaussian(camera_grain_hash(uvec4(uvec2(grain_id + 32768), frame_hash.y, frame_hash.z ^ p_view)));
}

// p_color: linear scene-referred, after exposure. p_auto_exposure_gain: the auto exposure factor applied on
// top of the camera's own exposure (1.0 without auto exposure); digital noise treats it as sensor gain.
vec3 apply_camera_grain(vec3 p_color, vec2 p_frag_coord, uint p_packed, float p_auto_exposure_gain, uint p_view) {
	uint type = p_packed & 3u;
	float cell = camera_grain_decode(p_packed, 2u, 7u, -8.0, 5.0);
	float amplitude_a = camera_grain_decode(p_packed, 9u, 8u, -16.0, 4.0);
	float amplitude_b = camera_grain_decode(p_packed, 17u, 7u, -16.0, 4.0);
	uint seed = p_packed >> 24u;

	vec4 n = camera_grain_noise(p_frag_coord, cell, seed, p_view);
	// Selwyn's law: noise of cells smaller than a pixel averages out within it, sigma * sqrt(area) staying
	// constant, so the image looks equally grainy at any resolution.
	float pixel_scale = min(cell, 1.0);

	if (type == CAMERA_GRAIN_DIGITAL) {
		// Photon shot noise grows with the square root of the signal, read noise is constant. Auto exposure acts
		// as gain (auto ISO): it amplifies the noise of the signal the camera actually captured.
		vec3 signal = max(p_color, vec3(0.0));
		float gain = p_auto_exposure_gain;
		vec3 sigma = sqrt(amplitude_a * amplitude_a * gain * signal + amplitude_b * amplitude_b * gain * gain);
		// A Bayer sensor gives red and blue a quarter of its photosites and green half, which raises their noise
		// by 2 and sqrt(2) over the full-sensor amplitudes.
		sigma *= vec3(2.0, 1.41421356, 2.0) * pixel_scale;
		return max(p_color + sigma * n.xyz, vec3(0.0));
	}

	// Film: grain is a fluctuation of the negative's density. Density rises with log exposure at the negative's
	// gamma (0.6, so 0.6 * log10(2) = 0.1806 per stop), from 1.0 at middle gray; granularity grows with the square
	// root of density, from the fog level in the deep shadows to the shoulder. Expressed as an exposure change,
	// the tonemapper (the print) then compresses it in the highlights as a print does.
	vec3 noise;
	vec3 level;
	if (type == CAMERA_GRAIN_FILM_MONOCHROME) {
		noise = n.www;
		level = vec3(dot(p_color, vec3(0.2126, 0.7152, 0.0722)));
	} else {
		// Each dye layer grains on its own, but the dye clouds overlap and scanning blurs them together; a
		// shared component of 65% keeps the grain mostly luminance with some color, as scanned color film looks.
		noise = n.www * 0.80622577 + n.xyz * 0.59160798;
		level = p_color;
	}
	vec3 density = clamp(1.0 + 0.1806 * log2(max(level, vec3(1e-6)) * (1.0 / 0.18)), vec3(0.1), vec3(2.8));
	return p_color * exp2(amplitude_a * pixel_scale * sqrt(density) * noise);
}
