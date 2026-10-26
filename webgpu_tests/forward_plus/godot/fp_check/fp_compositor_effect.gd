@tool
extends CompositorEffect
## The Forward+ matrix's compositor_effects arm: a user CompositorEffect that
## runs a compute shader over the scene's colour buffer after the transparent
## pass and inverts a rectangle of it (left half, middle third of the height).
##
## The effect is user code, but everything under it is engine: compiling GLSL to
## SPIR-V at run time (RenderingDevice.shader_compile_spirv_from_source), the
## driver's conversion of that SPIR-V, a read-write storage image bound to the
## scene's own colour target, push constants, and the compositor calling back in
## the middle of the frame. A broken link anywhere in that chain shows up either
## as no inversion (delta) or a wrong one (parity).

var rd: RenderingDevice
var shader: RID
var pipeline: RID

const SOURCE := """#version 450
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform image2D color_image;
layout(push_constant, std430) uniform Params {
	vec2 size;
	vec2 pad;
} params;
void main() {
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.size);
	if (uv.x >= size.x / 2 || uv.y < size.y / 3 || uv.y >= (size.y * 2) / 3) {
		return;
	}
	vec4 c = imageLoad(color_image, uv);
	c.rgb = vec3(1.0) - clamp(c.rgb, vec3(0.0), vec3(1.0));
	imageStore(color_image, uv, c);
}
"""


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	enabled = false
	RenderingServer.call_on_render_thread(_init_compute)


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE and shader.is_valid():
		RenderingServer.free_rid(shader)


func _init_compute() -> void:
	rd = RenderingServer.get_rendering_device()
	if not rd:
		print("[FP] COMPOSITOR no RenderingDevice")
		return
	var src := RDShaderSource.new()
	src.language = RenderingDevice.SHADER_LANGUAGE_GLSL
	src.source_compute = SOURCE
	var spirv := rd.shader_compile_spirv_from_source(src)
	if spirv.compile_error_compute != "":
		print("[FP] COMPOSITOR compile error: %s" % spirv.compile_error_compute)
		return
	shader = rd.shader_create_from_spirv(spirv)
	if shader.is_valid():
		pipeline = rd.compute_pipeline_create(shader)
	print("[FP] COMPOSITOR %s" % ("ready" if pipeline.is_valid() else "FAILED"))


func _render_callback(_type: int, render_data: RenderData) -> void:
	if not rd or not pipeline.is_valid():
		return
	var buffers: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
	if not buffers:
		return
	var size := buffers.get_internal_size()
	if size.x == 0 or size.y == 0:
		return
	var push := PackedFloat32Array([size.x, size.y, 0.0, 0.0]).to_byte_array()
	for view in buffers.get_view_count():
		var image := buffers.get_color_layer(view)
		var uniform := RDUniform.new()
		uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		uniform.binding = 0
		uniform.add_id(image)
		var set := UniformSetCacheRD.get_cache(shader, 0, [uniform])
		var list := rd.compute_list_begin()
		rd.compute_list_bind_compute_pipeline(list, pipeline)
		rd.compute_list_bind_uniform_set(list, set, 0)
		rd.compute_list_set_push_constant(list, push, push.size())
		rd.compute_list_dispatch(list, (size.x + 7) / 8, (size.y + 7) / 8, 1)
		rd.compute_list_end()
