extends SceneTree
# Custom tonemap test (task 56). Args (after --): out=<png> tm=linear|reinhard|filmic|aces|agx|custom
# [slope= toe= shoulder= black= white= exposure= scene=ramp|lit glow=0|1]
# scene=ramp: a 16x16 chart of unshaded patches, patch i (row-major from the top left) holding
# x_i = 2^(-12 + 18 i / 255) in red, x_(i+85 mod 256) in green and x_(i+170 mod 256) in blue (linear, HDR).
# scene=lit: a sphere and a box under a sun and a procedural sky (for the bit-identical checks).

var args := {}
var frames := 0
var out := ""

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
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	var tms := {"linear": 0, "reinhard": 1, "filmic": 2, "aces": 3, "agx": 4, "custom": 5}
	env.tonemap_mode = tms[args.get("tm", "linear")]
	env.tonemap_exposure = float(args.get("exposure", "1.0"))
	if args.get("tm", "") == "custom":
		if args.has("slope"): env.set("tonemap_custom_slope", float(args["slope"]))
		if args.has("toe"): env.set("tonemap_custom_toe", float(args["toe"]))
		if args.has("shoulder"): env.set("tonemap_custom_shoulder", float(args["shoulder"]))
		if args.has("black"): env.set("tonemap_custom_black_clip", float(args["black"]))
		if args.has("white"): env.set("tonemap_custom_white_clip", float(args["white"]))
	if args.has("twhite"):
		env.tonemap_white = float(args["twhite"])
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)
	var cam := Camera3D.new()
	if args.get("scene", "ramp") == "lit":
		env.background_mode = Environment.BG_SKY
		var sky := Sky.new()
		sky.sky_material = ProceduralSkyMaterial.new()
		env.sky = sky
		env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
		env.glow_enabled = args.get("glow", "0") == "1"
		var sun := DirectionalLight3D.new()
		sun.light_energy = 4.0
		sun.rotation = Vector3(-0.8, 0.6, 0.0)
		sun.shadow_enabled = true
		root3d.add_child(sun)
		var s := MeshInstance3D.new()
		s.mesh = SphereMesh.new()
		s.position = Vector3(-0.7, 0, 0)
		root3d.add_child(s)
		var b := MeshInstance3D.new()
		b.mesh = BoxMesh.new()
		b.position = Vector3(0.8, 0, 0)
		b.rotation = Vector3(0.3, 0.6, 0)
		var m := StandardMaterial3D.new()
		m.albedo_color = Color(0.9, 0.3, 0.1)
		m.emission_enabled = true
		m.emission = Color(1.0, 0.8, 0.2)
		m.emission_energy_multiplier = 3.0
		b.material_override = m
		root3d.add_child(b)
		var floor := MeshInstance3D.new()
		var pm := PlaneMesh.new()
		pm.size = Vector2(10, 10)
		floor.mesh = pm
		floor.position = Vector3(0, -1, 0)
		root3d.add_child(floor)
		cam.position = Vector3(0, 0.5, 3.5)
		cam.rotation = Vector3(-0.15, 0, 0)
	else:
		cam.projection = Camera3D.PROJECTION_ORTHOGONAL
		cam.size = 1.0
		cam.position = Vector3(0, 0, 1)
		root3d.add_child(make_chart())
	root3d.add_child(cam)
	root.add_child(root3d)

static func make_chart() -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(1, 1)
	mi.mesh = q
	var sh := Shader.new()
	sh.code = """
shader_type spatial;
render_mode unshaded;
float level(int i) {
	return exp2(-12.0 + 18.0 * float(i % 256) / 255.0);
}
void fragment() {
	ivec2 cell = ivec2(floor(vec2(UV.x, UV.y) * 16.0));
	int idx = cell.y * 16 + cell.x;
	ALBEDO = vec3(level(idx), level(idx + 85), level(idx + 170));
}
"""
	var m := ShaderMaterial.new()
	m.shader = sh
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
