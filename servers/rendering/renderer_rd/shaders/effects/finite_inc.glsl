// One NaN or infinity in the scene color (a single bad fragment) would otherwise cover the whole
// reach of an effect that filters or averages it: NaN * 0 is still NaN, so a weighted sum that
// includes it is NaN however small its weight. The bokeh DOF blanked its bokeh shape, glow its
// radius (or, where min() returns the other operand, lit a blob at the luminance cap), SSR the
// reflection's blur, and auto exposure the whole frame. Effects that read the scene color pass it
// through these first, so a non-finite color counts as black and one bad pixel stays one pixel.
//
// Tested on the exponent bits, never with isnan()/isinf() or x != x: compilers may assume floats
// are finite and fold those away (WGSL explicitly lets them). The select passes every finite value
// through bit-identical; the cost is an AND, a compare and a select per component.
//
// Distinct names rather than overloads: WGSL has no function overloading.

bool is_non_finite(float c) {
	return (floatBitsToUint(c) & 0x7f800000u) == 0x7f800000u;
}

bvec3 is_non_finite3(vec3 c) {
	return equal(floatBitsToUint(c) & uvec3(0x7f800000u), uvec3(0x7f800000u));
}

bvec4 is_non_finite4(vec4 c) {
	return equal(floatBitsToUint(c) & uvec4(0x7f800000u), uvec4(0x7f800000u));
}

float finite_or_zero(float c) {
	return is_non_finite(c) ? 0.0 : c;
}

vec3 finite_or_zero3(vec3 c) {
	return mix(c, vec3(0.0), is_non_finite3(c));
}

vec4 finite_or_zero4(vec4 c) {
	return mix(c, vec4(0.0), is_non_finite4(c));
}
