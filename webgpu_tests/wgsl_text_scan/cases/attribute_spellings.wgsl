// The same declarations in the spellings the two converters use, plus the ones a third could.
diagnostic(off, derivative_uniformity);
struct S { a : vec4f }
@group(0u) @binding(1u) var t0 : texture_2d<f32>;
@group(0) @binding(2) var t1: texture_2d<u32>;
@binding(3) @group(0) var t2 : texture_3d<i32>;
@group( 0 )
@binding( 4i )
var
t3
:
texture_cube_array<f32>;
@group(1) @binding(0) var<uniform> u : S;
@group(1) @binding(1) var<storage> sro : S;
@group(1) @binding(2) var<storage, read> sro2 : S;
@group(1) @binding(3) var<storage,read_write> srw : S;
@group(1) @binding(4) var<storage, read_write> srw2 : S;
@group(2) @binding(0) var s0 : sampler;
@group(2) @binding(1) var s1 : sampler_comparison;
@group(2) @binding(2) var d0 : texture_depth_2d;
@group(2) @binding(3) var d1 : texture_depth_multisampled_2d;
@group(2) @binding(4) var m0 : texture_multisampled_2d<f32>;
@group(2) @binding(5) var d2 : texture_depth_cube_array;
var<private> not_a_resource : f32;
