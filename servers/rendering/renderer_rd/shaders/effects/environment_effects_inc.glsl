// The EnvironmentEffects uniform buffer: parameters of the Environment's tonemap-pass features that do not fit the pass's
// push constant. Define ENVIRONMENT_EFFECTS_SET and ENVIRONMENT_EFFECTS_BINDING before including. The layout (std140,
// 256 bytes) must match RendererEnvironmentStorage::EnvironmentEffectsUBO (servers/rendering/storage/environment_storage.h).
//
// Each group owns one header word, whose bits say which of its features run, and a region:
// - header.x, tonemap_custom[3]: the Custom tonemapper's curve (tonemap_custom_inc.glsl).
// - header.yzw, reserved[12]: free, for later Environment post parameters.
// The renderers bind an all-zero buffer when nothing in it is used.

layout(set = ENVIRONMENT_EFFECTS_SET, binding = ENVIRONMENT_EFFECTS_BINDING, std140) uniform EnvironmentEffects {
	uvec4 header;
	vec4 tonemap_custom[3];
	vec4 reserved[12];
}
environment_effects;
