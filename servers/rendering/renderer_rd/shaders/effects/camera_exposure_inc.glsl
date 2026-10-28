// CameraAttributes exposure automation in the tonemap pass (camera_attributes_storage.cpp, "Exposure automation").
// CameraEffectsUBO::header[2] holds these bits (RendererCameraAttributes::ExposureBits). The metering modes need nothing
// here: they write the auto exposure texture the pass already reads.
//
// Before including, define CAMERA_EXPOSURE_SET and CAMERA_EXPOSURE_BINDING: where the auto white balance state texture
// (CameraMetering, camera_metering.glsl) is bound. Texel 0 holds the cone gains of the estimated illuminant.

#define CAMERA_EXPOSURE_AUTO_WHITE_BALANCE 1u

layout(set = CAMERA_EXPOSURE_SET, binding = CAMERA_EXPOSURE_BINDING) uniform sampler2D camera_auto_white_balance;

vec3 camera_auto_white_balance_gains() {
	return texelFetch(camera_auto_white_balance, ivec2(0, 0), 0).rgb;
}
