@id(0u) override A : u32 = 1u;
@id( 1 ) override B: f32 = 2.0f;
@id(2) override C : f32;
override derived : bool = (A == 0u);
@group(0) @binding(0) var w : texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(1) var r : texture_storage_2d<r32float,read>;
@group(0) @binding(2) var rw : texture_storage_3d< rgba16float , read_write >;
@compute @workgroup_size(8, 8, 1)
fn main() {
  let x = B;
  let y = textureLoad(r, vec2i(0));
  textureStore(w, vec2i(0), y);
  let z = textureLoad(rw,
    vec3i(0));
}
