#version 450
// 20 sampled textures already in use, then two arrays: the expansion may grow
// a module that is over WebGPU's default 16 up to the 48 Forward+ requires,
// so the first 16-element array (+15 = 37) fits and the second (+15 = 52)
// does not and stays collapsed to element 0.
layout(set = 0, binding = 0) uniform sampler samp;
layout(set = 0, binding = 1) uniform texture2D first[16];
layout(set = 0, binding = 2) uniform texture2D second[16];
layout(set = 0, binding = 4) uniform texture2D t0;
layout(set = 0, binding = 5) uniform texture2D t1;
layout(set = 0, binding = 6) uniform texture2D t2;
layout(set = 0, binding = 7) uniform texture2D t3;
layout(set = 0, binding = 8) uniform texture2D t4;
layout(set = 0, binding = 9) uniform texture2D t5;
layout(set = 0, binding = 10) uniform texture2D t6;
layout(set = 0, binding = 11) uniform texture2D t7;
layout(set = 0, binding = 12) uniform texture2D t8;
layout(set = 0, binding = 13) uniform texture2D t9;
layout(set = 0, binding = 14) uniform texture2D t10;
layout(set = 0, binding = 15) uniform texture2D t11;
layout(set = 0, binding = 16) uniform texture2D t12;
layout(set = 0, binding = 17) uniform texture2D t13;
layout(set = 0, binding = 18) uniform texture2D t14;
layout(set = 0, binding = 19) uniform texture2D t15;
layout(set = 0, binding = 20) uniform texture2D t16;
layout(set = 0, binding = 21) uniform texture2D t17;
layout(set = 0, binding = 22) uniform texture2D t18;
layout(set = 0, binding = 23) uniform texture2D t19;

layout(location = 0) in vec2 uv;
layout(location = 1) flat in uint idx;
layout(location = 0) out vec4 o;

void main() {
	vec4 sum = vec4(0.0);
	sum += texture(sampler2D(t0, samp), uv);
	sum += texture(sampler2D(t1, samp), uv);
	sum += texture(sampler2D(t2, samp), uv);
	sum += texture(sampler2D(t3, samp), uv);
	sum += texture(sampler2D(t4, samp), uv);
	sum += texture(sampler2D(t5, samp), uv);
	sum += texture(sampler2D(t6, samp), uv);
	sum += texture(sampler2D(t7, samp), uv);
	sum += texture(sampler2D(t8, samp), uv);
	sum += texture(sampler2D(t9, samp), uv);
	sum += texture(sampler2D(t10, samp), uv);
	sum += texture(sampler2D(t11, samp), uv);
	sum += texture(sampler2D(t12, samp), uv);
	sum += texture(sampler2D(t13, samp), uv);
	sum += texture(sampler2D(t14, samp), uv);
	sum += texture(sampler2D(t15, samp), uv);
	sum += texture(sampler2D(t16, samp), uv);
	sum += texture(sampler2D(t17, samp), uv);
	sum += texture(sampler2D(t18, samp), uv);
	sum += texture(sampler2D(t19, samp), uv);
	o = sum + texture(sampler2D(first[idx], samp), uv) + texture(sampler2D(second[idx], samp), uv);
}
