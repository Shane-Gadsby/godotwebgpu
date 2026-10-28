# Lens optics verification scenes. Usage (after --):
#   scene=<flat|dots|edge|default> attrs=<physical|practical|none> out=<png> [frames=N] [key=value ...]
# key=value pairs set CameraAttributes properties (floats, ints, bools, or "x,y" Vector2s).
extends SceneTree

var args := {}
var frames := 0
var cam: Camera3D

func _parse_value(v: String):
	if v == "true":
		return true
	if v == "false":
		return false
	if v.contains(","):
		var p := v.split(",")
		return Vector2(float(p[0]), float(p[1]))
	if v.is_valid_int():
		return int(v)
	return float(v)

func _quad(pos: Vector3, size: float, color: Color) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	var q := QuadMesh.new()
	q.size = Vector2(size, size)
	mi.mesh = q
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = color
	mi.material_override = m
	mi.position = pos
	return mi

func _init():
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	var root3d := Node3D.new()
	cam = Camera3D.new()
	cam.position = Vector3(0, 0, 0)
	root3d.add_child(cam)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_COLOR
	e.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var bg := float(args.get("bg", "0.0"))
	e.background_color = Color(bg, bg, bg)
	if args.get("glow", "false") == "true":
		e.glow_enabled = true
	env.environment = e
	root3d.add_child(env)

	var scene: String = args.get("scene", "flat")
	if scene == "dots":
		# A grid of small white squares on a plane 10 m away (unshaded).
		var n := int(args.get("n", "9"))
		var span := float(args.get("span", "12.0"))
		for j in range(-(n / 2), n / 2 + 1):
			for i in range(-(n / 2) - 3, n / 2 + 4):
				root3d.add_child(_quad(Vector3(i * span / n, j * span / n, -10.0), float(args.get("dot", "0.08")), Color(1, 1, 1)))
	elif scene == "edge":
		# Left half white, right half black: a vertical edge through the center at 100 m.
		var q := _quad(Vector3(-500.0, 0, -100.0), 1000.0, Color(1, 1, 1))
		root3d.add_child(q)
	elif scene == "default":
		# Something with lights, shading and glow for the bit-identical check.
		e.background_mode = Environment.BG_SKY
		e.sky = Sky.new()
		e.sky.sky_material = ProceduralSkyMaterial.new()
		e.tonemap_mode = Environment.TONE_MAPPER_FILMIC
		e.glow_enabled = true
		var light := DirectionalLight3D.new()
		light.rotation_degrees = Vector3(-50, 30, 0)
		light.shadow_enabled = true
		root3d.add_child(light)
		for i in 5:
			var mi := MeshInstance3D.new()
			mi.mesh = SphereMesh.new()
			mi.position = Vector3(i * 1.5 - 3.0, 0, -6.0)
			var m := StandardMaterial3D.new()
			m.albedo_color = Color.from_hsv(i / 5.0, 0.7, 0.9)
			m.emission_enabled = i == 2
			m.emission = Color(1, 0.5, 0.2)
			m.emission_energy_multiplier = 4.0
			mi.material_override = m
			root3d.add_child(mi)
	# "flat": just the background color.

	var attrs_kind: String = args.get("attrs", "none")
	var attrs: CameraAttributes = null
	if attrs_kind == "physical":
		attrs = CameraAttributesPhysical.new()
	elif attrs_kind == "practical":
		attrs = CameraAttributesPractical.new()
	if attrs:
		for k in args:
			if k in ["scene", "attrs", "out", "frames", "n", "span", "dot", "bg", "glow", "fov", "print"]:
				continue
			attrs.set(k, _parse_value(args[k]))
		cam.attributes = attrs
	if args.has("fov"):
		cam.fov = float(args["fov"])
	root.add_child(root3d)

func _process(_delta):
	frames += 1
	if frames == int(args.get("frames", "12")):
		var img := root.get_viewport().get_texture().get_image()
		img.save_png(args.get("out", "out.png"))
		var info := {"fov": cam.fov, "keep_aspect": cam.keep_aspect, "size": [img.get_width(), img.get_height()]}
		print("LENSINFO ", JSON.stringify(info))
		quit()
	return false
