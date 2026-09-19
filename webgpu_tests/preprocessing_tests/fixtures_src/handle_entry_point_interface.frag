#version 450

// Regression fixture for strip_handle_vars_from_entry_point_interface
// (drivers/webgpu/spirv_preprocess.cpp). Compiled at --target-env vulkan1.2
// (not compile_fixtures.sh's default env) specifically to get SPIR-V 1.4+,
// the version whose broader OpEntryPoint interface-list rule (every
// module-scope variable an entry point uses must be listed, not just
// Input/Output) triggers a real Tint SPIR-V reader bug: Tint emits an
// invalid `_ = &(var);` "phony" reference for every listed interface id
// regardless of storage class, which is illegal WGSL for a Handle-address-
// space (sampler/texture) variable. See run_tests.mjs's
// "strip_handle_vars_from_entry_point_interface" test.
//
// A separately-declared texture2D + sampler (rather than a combined
// sampler2D) is what actually exercises this -- it's the exact shape
// Godot's own production shaders use throughout (e.g.
// canvas_uniforms_inc.glsl's texture_sampler), and is also
// split_combined_samplers' own output shape for every ordinary combined
// sampler2D uniform.

layout(set = 0, binding = 0) uniform texture2D color_texture;
layout(set = 0, binding = 1) uniform sampler linear_sampler;

layout(location = 0) out vec4 frag_color;
layout(location = 0) in vec2 uv;

vec4 sample_tex(texture2D tex, sampler samp, vec2 coords) {
    return texture(sampler2D(tex, samp), coords);
}

void main() {
    frag_color = sample_tex(color_texture, linear_sampler, uv);
}
