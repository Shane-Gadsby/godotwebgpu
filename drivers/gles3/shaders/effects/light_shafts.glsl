/* clang-format off */
#[modes]

// Screen-space light shafts, the Compatibility renderer's copy of servers/rendering/renderer_rd/shaders/effects/
// light_shafts.glsl (the model and its sources are there and in servers/rendering/storage/environment_storage.cpp). The composite
// is in post.glsl (USE_LIGHT_SHAFTS), where the frame is linear again.

mode_mask = #define MODE_MASK
mode_blur = #define MODE_BLUR

#[specializations]

#[vertex]
layout(location = 0) in vec2 vertex_attrib;

/* clang-format on */

out vec2 uv_interp;

void main() {
	uv_interp = vertex_attrib * 0.5 + 0.5;
	gl_Position = vec4(vertex_attrib, 1.0, 1.0);
}

/* clang-format off */
#[fragment]
/* clang-format on */

// Both buffers hold light times this, so an 8- or 10-bit fallback buffer keeps a range of 4 (as glow's luminance
// multiplier does); a half-float buffer stores light as it is.
uniform float encode_scale;

in vec2 uv_interp;

layout(location = 0) out vec4 frag_color;

#ifdef MODE_MASK

uniform sampler2D source_color; // texunit:0
uniform sampler2D depth_buffer; // texunit:1
// Of the source.
uniform vec2 pixel_size;
uniform float luminance_multiplier;
uniform float threshold;

// The same approximation as tonemap_inc.glsl's, which post.glsl decodes the frame with.
vec3 srgb_to_linear(vec3 color) {
	return color * (color * (color * 0.305306011 + 0.682171111) + 0.012522878);
}

void main() {
	vec3 mask = vec3(0.0);
	for (int i = 0; i < 4; i++) {
		vec2 offset = vec2(float(i & 1), float(i >> 1)) * 2.0 - 1.0;
		vec2 uv = uv_interp + offset * pixel_size;
		// The sky is at the far plane, depth 1 (cleared, or drawn there); geometry, however far, occludes.
		float depth = textureLod(depth_buffer, uv, 0.0).r;
		if (depth < 1.0) {
			continue;
		}
		vec3 color = srgb_to_linear(max(textureLod(source_color, uv, 0.0).rgb / luminance_multiplier, vec3(0.0)));
		float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722)); // ITU-R BT.709 luma of linear light.
		mask += color * (max(luminance - threshold, 0.0) / max(luminance, 1e-4));
	}
	frag_color = vec4(mask * 0.25 * encode_scale, 1.0);
}

#endif

#ifdef MODE_BLUR

uniform sampler2D mask_texture; // texunit:0
// xy: the sun in uv when z is 1, else the direction toward it; w: aspect (RendererEnvironmentStorage::LightShaftsSun).
uniform vec4 sun;
uniform float decay;
uniform float max_length;
uniform int sample_count;

float interleaved_gradient_noise(vec2 p_pos) {
	// Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014).
	return fract(52.9829189 * fract(dot(p_pos, vec2(0.06711056, 0.00583715))));
}

vec3 sample_mask(vec2 p_pos) {
	vec2 uv = vec2(p_pos.x / sun.w, p_pos.y);
	if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
		return vec3(0.0);
	}
	return textureLod(mask_texture, uv, 0.0).rgb;
}

void main() {
	float aspect = sun.w;
	vec2 pos = vec2(uv_interp.x * aspect, uv_interp.y);
	vec2 to_sun;
	float distance_to_sun;
	if (sun.z > 0.5) {
		to_sun = vec2(sun.x * aspect, sun.y) - pos;
		distance_to_sun = length(to_sun);
	} else {
		to_sun = sun.xy;
		distance_to_sun = 1e10;
	}
	if (distance_to_sun < 1e-6) {
		frag_color = vec4(sample_mask(pos), 1.0);
		return;
	}
	vec2 dir = to_sun / length(to_sun);
	float march = min(distance_to_sun, max_length);

	float tail = exp(-decay * march);
	float jitter = interleaved_gradient_noise(gl_FragCoord.xy);
	float n = float(sample_count);
	vec3 sum = vec3(0.0);
	for (int i = 0; i < sample_count; i++) {
		float v = (float(i) + jitter) / n;
		float t = -log(1.0 - v * (1.0 - tail)) / decay;
		sum += sample_mask(pos + dir * t);
	}
	// The mask is already scaled by encode_scale, and so is this.
	frag_color = vec4(sum * ((1.0 - tail) / n) + sample_mask(pos + dir * march) * tail, 1.0);
}

#endif
