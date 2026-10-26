extends CompositorEffect
# Scans the HDR color buffer after the transparent pass (before DOF) for non-finite values and
# records the first 64 hits with their raw bits. Reads only: it changes nothing it scans.

var rd: RenderingDevice
var shader: RID
var pipeline: RID
var buf: RID
var last := PackedByteArray()
var frames := 0
var region := Vector2i(528, 288)

const SRC := """
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba16f, set = 0, binding = 0) uniform readonly image2D color;
layout(set = 0, binding = 1, std430) buffer Out { uint count; uint pad0, pad1, pad2; uvec4 hits[128]; vec4 region[256]; } o;
layout(push_constant) uniform PC { ivec4 r; } pc;
void main() {
	ivec2 p = ivec2(gl_GlobalInvocationID.xy);
	ivec2 s = imageSize(color);
	if (p.x >= s.x || p.y >= s.y) return;
	vec4 c = imageLoad(color, p);
	ivec2 d = p - pc.r.xy + ivec2(8);
	if (all(greaterThanEqual(d, ivec2(0))) && all(lessThan(d, ivec2(16)))) {
		o.region[d.y * 16 + d.x] = c;
	}
	bvec4 bad = bvec4(isnan(c.r) || isinf(c.r), isnan(c.g) || isinf(c.g), isnan(c.b) || isinf(c.b), isnan(c.a) || isinf(c.a));
	if (any(bad)) {
		uint i = atomicAdd(o.count, 1u);
		if (i < 64u) {
			o.hits[i * 2u] = uvec4(uint(p.x), uint(p.y), packHalf2x16(c.rg), packHalf2x16(c.ba));
			o.hits[i * 2u + 1u] = floatBitsToUint(c);
		}
	}
}
"""

func _init():
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(_setup)

func _setup():
	var src := RDShaderSource.new()
	src.language = RenderingDevice.SHADER_LANGUAGE_GLSL
	src.source_compute = SRC
	var spirv := rd.shader_compile_spirv_from_source(src)
	if spirv.compile_error_compute != "":
		push_error(spirv.compile_error_compute)
		return
	shader = rd.shader_create_from_spirv(spirv)
	pipeline = rd.compute_pipeline_create(shader)
	var init := PackedByteArray()
	init.resize(16 + 128 * 16 + 256 * 16)
	buf = rd.storage_buffer_create(init.size(), init)

func _render_callback(_t: int, render_data: RenderData):
	if not pipeline.is_valid():
		return
	var sb: RenderSceneBuffersRD = render_data.get_render_scene_buffers()
	var size := sb.get_internal_size()
	var img: RID = sb.get_color_layer(0)
	var zero := PackedByteArray()
	zero.resize(16)
	rd.buffer_update(buf, 0, 16, zero)
	var u0 := RDUniform.new()
	u0.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
	u0.binding = 0
	u0.add_id(img)
	var u1 := RDUniform.new()
	u1.uniform_type = RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER
	u1.binding = 1
	u1.add_id(buf)
	var set := UniformSetCacheRD.get_cache(shader, 0, [u0, u1])
	var cl := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(cl, pipeline)
	rd.compute_list_bind_uniform_set(cl, set, 0)
	var pc := PackedInt32Array([region.x, region.y, 0, 0]).to_byte_array()
	rd.compute_list_set_push_constant(cl, pc, pc.size())
	rd.compute_list_dispatch(cl, (size.x + 7) / 8, (size.y + 7) / 8, 1)
	rd.compute_list_end()
	last = rd.buffer_get_data(buf)
	frames += 1

func report() -> String:
	if last.is_empty():
		return "NANPROBE no data"
	var n: int = last.decode_u32(0)
	var s := "NANPROBE frames=%d nonfinite_pixels=%d" % [frames, n]
	for i in min(n, 64):
		var o: int = 16 + i * 32
		var x: int = last.decode_u32(o)
		var y: int = last.decode_u32(o + 4)
		var bits := []
		var vals := []
		for c in 4:
			var b: int = last.decode_u32(o + 16 + c * 4)
			bits.append("%08x" % b)
			vals.append(last.decode_float(o + 16 + c * 4))
		s += "\n  (%d,%d) rgba=%s bits=%s" % [x, y, str(vals), " ".join(bits)]
	s += "\n  region around %s (rows y-8..y+7, cols x-8..x+7), rgba:" % str(region)
	var base := 16 + 128 * 16
	for yy in 16:
		var row := []
		for xx in 16:
			var o: int = base + (yy * 16 + xx) * 16
			row.append("%s/%s/%s/%s" % [snapped(last.decode_float(o), 0.0001), snapped(last.decode_float(o + 4), 0.0001), snapped(last.decode_float(o + 8), 0.0001), snapped(last.decode_float(o + 12), 0.0001)])
		s += "\n   y=%d: %s" % [region.y - 8 + yy, "  ".join(row)]
	return s
