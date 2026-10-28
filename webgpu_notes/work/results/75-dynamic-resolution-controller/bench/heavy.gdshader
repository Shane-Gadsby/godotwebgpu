shader_type spatial;
// Per-pixel cost proportional to `iters`: makes the GPU frame scale with the 3D render resolution.
uniform int iters = 64;
uniform vec3 tint = vec3(0.6, 0.5, 0.4);
float h(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void fragment() {
	vec2 p = UV * 40.0;
	float a = 0.0;
	for (int i = 0; i < iters; i++) {
		p = vec2(p.x * 0.8 - p.y * 0.6, p.x * 0.6 + p.y * 0.8) * 1.01 + vec2(0.3, 0.7);
		a += h(floor(p)) * 0.02;
	}
	ALBEDO = tint * (0.5 + 0.5 * fract(a));
	ROUGHNESS = 0.3 + 0.5 * fract(a * 3.0);
	METALLIC = 0.2;
}
