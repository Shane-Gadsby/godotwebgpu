extends SceneTree
# Color grading test (task 45): a chart of unshaded patches whose tonemapped, sRGB-encoded value is exactly a known
# color, under an Environment with one grading case. Args (after --): out=<png> case=<name> chart=lattice|off
# Chart "lattice": 9 levels i/8 per channel (729 patches, on the 33^3 LUT's lattice); "off": (i+0.5)/8, 8 levels.

var args := {}
var frames := 0
var out := ""
const Cases = preload("res://cases.gd")

func _init() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	out = args.get("out", "/tmp/out.png")
	var root3d := Node3D.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0, 0, 0)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	Cases.apply(env, args.get("case", "off"))
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)
	var cam := Camera3D.new()
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = 1.0
	cam.position = Vector3(0, 0, 1)
	root3d.add_child(cam)
	root3d.add_child(make_chart(args.get("chart", "lattice")))
	root.add_child(root3d)

static func make_chart(chart: String) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(1, 1)
	mi.mesh = q
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode unshaded;
uniform int levels;
uniform int side;
uniform float offset;
void fragment() {
	ivec2 cell = ivec2(floor(UV * float(side)));
	int idx = cell.y * side + cell.x;
	vec3 c = vec3(float(idx % levels), float((idx / levels) % levels), float(idx / (levels * levels)));
	c = (c + offset) / 8.0;
	if (idx >= levels * levels * levels) {
		c = vec3(0.0);
	}
	// sRGB-encoded c, in linear light: the linear tonemapper and the output encoding give c back.
	ALBEDO = mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, lessThan(c, vec3(0.04045)));
}
"""
	var m := ShaderMaterial.new()
	m.shader = sh
	if chart == "off":
		m.set_shader_parameter("levels", 8)
		m.set_shader_parameter("side", 23)
		m.set_shader_parameter("offset", 0.5)
	else:
		m.set_shader_parameter("levels", 9)
		m.set_shader_parameter("side", 27)
		m.set_shader_parameter("offset", 0.0)
	mi.material_override = m
	return mi

func _process(_delta: float) -> bool:
	frames += 1
	if frames == 12:
		var img := root.get_viewport().get_texture().get_image()
		img.save_png(out)
		print("saved ", out)
		quit()
	return false
