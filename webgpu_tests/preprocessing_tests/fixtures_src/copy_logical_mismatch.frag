#version 450

// Regression fixture for rewrite_copy_logical's decompose path
// (drivers/webgpu/spirv_preprocess.cpp). Compiled at --target-env vulkan1.2
// (not compile_fixtures.sh's default env) specifically to get SPIR-V 1.4,
// the version that actually emits OpCopyLogical -- see run_tests.mjs's
// "rewrite_copy_logical: real-world type-mismatch case" test for why.
//
// Copying a std140 UBO block member into a plain local variable of the
// "bare" struct type forces glslang to emit OpCopyLogical between two
// distinctly-declared-but-structurally-identical OpTypeStruct ids (Vulkan
// requires the interface-block member to carry its own Offset/MatrixStride
// decorations, which the bare local-variable type can't share). This is
// exactly the shape of Godot's own SceneData UBO -> local copy that was
// failing Tint conversion in production (webgpu_notes/TASKS.md Task 9.2).

struct SceneData {
    vec4 a;
    mat4 b;
    vec3 c;
    float d;
};

layout(set = 0, binding = 0, std140) uniform SceneBlock {
    SceneData scene_data;
} scene_data_block;

layout(location = 0) out vec4 frag_color;

vec4 use_scene(SceneData s) {
    return s.a + vec4(s.c, s.d) + s.b[0];
}

void main() {
    SceneData local = scene_data_block.scene_data;
    frag_color = use_scene(local);
}
