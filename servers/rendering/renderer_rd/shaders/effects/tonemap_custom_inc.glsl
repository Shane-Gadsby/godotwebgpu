// The Custom tonemapper's curve: Unreal Engine's Film tonemapper parameters (slope, toe, shoulder, black clip, white
// clip) on a straight line in log(x) joined to a tanh toe and shoulder. The model, its sources and the constants'
// derivation are in RendererEnvironmentStorage::tonemap_custom_compute_curve() (servers/rendering/storage/
// environment_storage.cpp); this evaluates it from those constants, in log2(x):
//   p0 = (toe end, shoulder start, line offset, slope), p1 = (toe end value, toe scale, shoulder start value,
//   shoulder scale), p2 = (blend scale, blend offset, slope / toe scale, slope / shoulder scale).
// A copy for the Compatibility renderer is in drivers/gles3/shaders/tonemap_custom_inc.glsl.

vec3 tonemap_custom_curve(vec3 x, vec4 p0, vec4 p1, vec4 p2) {
	// 1e-10 is far below anything a color buffer holds; it keeps log2() finite, and the toe is at its floor there.
	vec3 l = log2(max(x, vec3(1e-10)));
	vec3 line = p0.w * (l + p0.z);
	// tanh() of a clamped argument: |tanh(10)| rounds to 1.0 in single precision, and some implementations of tanh()
	// overflow to NaN for large arguments.
	vec3 toe = p1.x + p1.y * tanh(clamp(p2.z * (l - p0.x), vec3(-10.0), vec3(10.0)));
	vec3 shoulder = p1.z + p1.w * tanh(clamp(p2.w * (l - p0.y), vec3(-10.0), vec3(10.0)));
	toe = mix(toe, line, greaterThanEqual(l, vec3(p0.x)));
	shoulder = mix(line, shoulder, greaterThan(l, vec3(p0.y)));
	vec3 t = clamp(l * p2.x + p2.y, vec3(0.0), vec3(1.0));
	return clamp(mix(toe, shoulder, t * t * (3.0 - 2.0 * t)), vec3(0.0), vec3(1.0));
}
