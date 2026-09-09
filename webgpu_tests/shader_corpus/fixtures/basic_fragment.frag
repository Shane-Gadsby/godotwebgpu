#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_world_pos;

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform CameraData {
    mat4 view_projection;
    vec4 camera_pos;
} camera;

layout(set = 1, binding = 0) uniform sampler2D albedo_tex;
layout(set = 1, binding = 1) uniform sampler2D normal_tex;

layout(push_constant) uniform PushConstants {
    mat4 model_matrix;
    uint instance_index;
    float time;
} pc;

void main() {
    // Every declared uniform/sampler/push-constant is genuinely read below —
    // this fixture exercises Tint pass interaction (combined sampler split +
    // push constants + depth images), and drivers/webgpu/spirv_preprocess.cpp's
    // eliminate_dead_resources pass now strips anything declared-but-unused
    // before Tint ever sees it (see webgpu_notes/TASKS.md Task 8.7), so a
    // fixture that declares something it never reads no longer round-trips
    // that declaration into the WGSL output at all.
    vec4 world_pos = pc.model_matrix * vec4(v_world_pos, 1.0);
    vec4 clip_pos = camera.view_projection * world_pos;

    vec4 albedo = texture(albedo_tex, v_uv);
    vec3 tex_normal = normalize(texture(normal_tex, v_uv).rgb * 2.0 - 1.0);
    vec3 N = normalize(v_normal + tex_normal);
    vec3 L = normalize(camera.camera_pos.xyz - v_world_pos);
    float NdotL = max(dot(N, L), 0.0);
    float pulse = 0.5 + 0.5 * sin(pc.time + float(pc.instance_index));

    frag_color = vec4(albedo.rgb * (0.2 + 0.8 * NdotL) * pulse, albedo.a * clamp(clip_pos.w, 0.0, 1.0));
}
