extends CompositorEffect
# Counts non-finite pixels in the HDR scene color after the transparent pass, before any
# post-processing: reads only, so it cannot hide the value it is looking for (nan_check.gd).

var rd: RenderingDevice
var shader: RID
var pipeline: RID
var buf: RID
var count := -1

const SRC := """
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, set = 0, binding = 0) uniform readonly image2D color;
layout(set = 0, binding = 1, std430) buffer Out { uint count; } o;
void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(p, imageSize(color)))) {
		return;
	}
	uvec4 e = floatBitsToUint(imageLoad(color, p)) & 0x7f800000u;
	if (any(equal(e, uvec4(0x7f800000u)))) {
		atomicAdd(o.count, 1u);
	}
}
"""


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(_setup)


func _setup() -> void:
	var src := RDShaderSource.new()
	src.source_compute = SRC
	var spirv := rd.shader_compile_spirv_from_source(src)
	if spirv.compile_error_compute != "":
		push_error(spirv.compile_error_compute)
		return
	shader = rd.shader_create_from_spirv(spirv)
	pipeline = rd.compute_pipeline_create(shader)
	buf = rd.storage_buffer_create(4, PackedByteArray([0, 0, 0, 0]))


func _render_callback(_type: int, render_data: RenderData) -> void:
	if not pipeline.is_valid():
		return
	var sb: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
	var size := sb.get_internal_size()
	rd.buffer_update(buf, 0, 4, PackedByteArray([0, 0, 0, 0]))
	var u0 := RDUniform.new()
	u0.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u0.binding = 0
	u0.add_id(sb.get_color_layer(0))
	var u1 := RDUniform.new()
	u1.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u1.binding = 1
	u1.add_id(buf)
	var cl := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(cl, pipeline)
	rd.compute_list_bind_uniform_set(cl, UniformSetCacheRD.get_cache(shader, 0, [u0, u1]), 0)
	rd.compute_list_dispatch(cl, (size.x + 7) / 8, (size.y + 7) / 8, 1)
	rd.compute_list_end()
	count = rd.buffer_get_data(buf).decode_u32(0)


func free_resources() -> void:
	for r in [buf, pipeline, shader]:
		if r.is_valid():
			rd.free_rid(r)
