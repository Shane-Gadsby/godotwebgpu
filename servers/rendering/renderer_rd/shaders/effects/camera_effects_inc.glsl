// The CameraEffects uniform buffer: parameters of the CameraAttributes effects the tonemap pass applies that do not
// fit its push constant. Define CAMERA_EFFECTS_SET and CAMERA_EFFECTS_BINDING before including. The layout (std140,
// 512 bytes) must match RendererCameraAttributes' CameraEffectsUBO (servers/rendering/storage/camera_attributes_storage.h)
// and the Compatibility renderer's copy in drivers/gles3/shaders/effects/camera_effects_inc.glsl.
//
// Each group of effects owns a region and one header word, whose bits say which of its effects run:
// - header.x, lens[8]: lens optics (camera_lens_inc.glsl).
// - header.y, glare[6]: reserved for lens flare, veiling glare and diffusion filters.
// - header.z, exposure[2]: reserved for exposure automation.
// - header.w, reserved[15]: free.
// The renderers bind an all-zero buffer when no effect is on, so every effect sits behind a uniform branch on its
// header word and the output is bit-identical to having no buffer.

layout(set = CAMERA_EFFECTS_SET, binding = CAMERA_EFFECTS_BINDING, std140) uniform CameraEffects {
	uvec4 header;
	vec4 lens[8];
	vec4 glare[6];
	vec4 exposure[2];
	vec4 reserved[15];
}
camera_effects;
