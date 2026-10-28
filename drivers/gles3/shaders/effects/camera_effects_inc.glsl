// The CameraEffects uniform buffer for the Compatibility renderer's post pass. A copy of
// servers/rendering/renderer_rd/shaders/effects/camera_effects_inc.glsl, which documents it: keep the two in step. The
// layout (std140, 512 bytes) must match RendererCameraAttributes' CameraEffectsUBO. Bound at uniform buffer binding 16
// (RasterizerSceneGLES3's CAMERA_EFFECTS_UNIFORM_LOCATION), which no other shader uses.

layout(std140) uniform CameraEffectsData { //ubo:16
	uvec4 header;
	vec4 lens[8];
	vec4 glare[6];
	vec4 exposure[2];
	// Local exposure: Forward+ only, never set here.
	vec4 local_exposure[2];
	vec4 reserved[13];
}
camera_effects;
