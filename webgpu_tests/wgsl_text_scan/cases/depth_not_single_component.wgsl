@group(0) @binding(1) var source_depth : texture_2d<f32>;
fn f(p : vec2i) -> vec4f {
    let _27 = textureLoad(source_depth, p, 0);
    let _30 = _27.x;
    return _27;
}
