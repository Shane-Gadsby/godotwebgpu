// SPIRV-Cross keeps the vec4 in a let and swizzles at each use; Tint puts the swizzle on the call.
@group(0) @binding(1) var source_depth : texture_2d<f32>;
@group(0) @binding(2) var other_depth : texture_2d<f32>;
@group(0) @binding(3) var a_sampler : sampler;
fn f(p : vec2i) -> f32 {
    let _27 = textureLoad(source_depth, p, 0);
    let _30 = _27.x;
    return _30 + textureSampleLevel(other_depth, a_sampler, vec2f(0.0f), 0.0f).r;
}
