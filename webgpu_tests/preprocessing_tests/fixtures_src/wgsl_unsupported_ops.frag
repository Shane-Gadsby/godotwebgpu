#version 450
// Each construct below used to make Tint's SPIR-V reader abort (an internal
// compiler error that kills the whole wasm instance in the browser):
// OpIsNan/OpIsInf, OpImageQueryLod, the PointCoord built-in and the
// EarlyFragmentTests execution mode. lower_wgsl_unsupported_ops() rewrites them.
layout(early_fragment_tests) in;

layout(set = 0, binding = 0) uniform texture2D tex;
layout(set = 0, binding = 1) uniform sampler samp;

layout(location = 0) in vec4 v;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 o;

void main() {
	bvec2 nan = isnan(v.xy);
	bool inf = isinf(v.z);
	vec2 lod = textureQueryLod(sampler2D(tex, samp), uv);
	o = vec4(nan.x || nan.y ? 1.0 : 0.0, inf ? 1.0 : 0.0, lod.x + lod.y, gl_PointCoord.x + gl_PointCoord.y);
}
