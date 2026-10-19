#version 450
#extension GL_EXT_samplerless_texture_functions : require
// Handle arrays as the engine uses them: VoxelGI's texture3D[8] indexed at
// runtime (also through a helper taking the texture, as voxel_cone_trace()
// does), and a lightmap-style texture2DArray[8] indexed with an offset
// (lightmap_textures[MAX_LIGHTMAP_TEXTURES + ofs]) and with a constant.
// expand_binding_arrays() gives every element a binding of its own: 2 + 7 + 7
// sampled textures exactly fill WebGPU's default per-stage limit of 16.
layout(set = 1, binding = 1) uniform sampler samp;
layout(set = 1, binding = 7) uniform texture2DArray lm[8];
layout(set = 1, binding = 8) uniform texture3D vox[8];

layout(location = 0) in vec3 p;
layout(location = 1) flat in uint idx;
layout(location = 0) out vec4 o;

vec4 cone(texture3D t, vec3 pos) {
	vec4 acc = vec4(0.0);
	for (int i = 0; i < 4; i++) {
		acc += textureLod(sampler3D(t, samp), pos * float(i), float(i));
	}
	return acc;
}

void main() {
	vec4 a = textureLod(sampler3D(vox[idx], samp), p, 0.0);
	vec4 b = texture(sampler2DArray(lm[4 + idx], samp), p);
	vec4 c = texture(sampler2DArray(lm[3], samp), p);
	for (uint i = 0; i < idx; i++) {
		a += textureLod(sampler3D(vox[i], samp), p, 1.0);
	}
	a += cone(vox[idx], p);
	o = a + b + c + vec4(textureSize(vox[idx], 0), 1.0);
}
