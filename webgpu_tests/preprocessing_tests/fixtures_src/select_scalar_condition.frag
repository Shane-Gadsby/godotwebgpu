#version 450

// Regression fixture for broadcast_select_scalar_condition
// (drivers/webgpu/spirv_preprocess.cpp). Compiled at --target-env vulkan1.2
// specifically to get real SPIR-V 1.4+ output -- at that version glslang
// emits OpSelect with a *scalar* bool Condition selecting a vector Result
// Type directly (valid only at 1.4+; SPV_ENV_VULKAN_1_2 is what Godot's own
// glslang integration actually targets, see inline_opaque_functions's
// comment). strip_handle_vars_from_entry_point_interface (Task 18) then
// downgrades the module below 1.4 for an unrelated reason, which makes this
// otherwise-untouched OpSelect invalid unless broadcast_select_scalar_condition
// fixes it up first. Compiling this same source at a lower target env
// (compile_fixtures.sh's default, like every other fixture in this suite)
// would not reproduce the bug at all: at that version glslang itself already
// emits the correct broadcasted condition. See webgpu_notes/TASKS.md Task 20.

layout(location = 0) out vec4 frag_color;
layout(location = 0) in vec3 v_a;
layout(location = 1) in vec3 v_b;
layout(location = 2) flat in int v_flag;

void main() {
    bool cond = v_flag != 0;
    vec3 result = cond ? v_a : v_b;
    frag_color = vec4(result, 1.0);
}
