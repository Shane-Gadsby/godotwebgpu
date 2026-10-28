extends SceneTree
# glare_test.gd -- args after "--": out=<png> scene=<default|points|ground|halves|single> attrs=<physical|practical|none>
#   frames=<n> and any number of cam.<property>=<value> / env.<property>=<value> (values parsed with str_to_var),
#   plus points=<x,y;x,y;...> (frame-relative positions of emissive points, -1..1) and point_size, point_energy, point_depth.

var args := {}
var frame := 0

func _init() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		args[kv[0]] = kv[1] if kv.size() > 1 else ""
	if args.get("hdr", "0") == "1":
		get_root().use_hdr_2d = true
	var root3d := Node3D.new()
	get_root().add_child(root3d)
	var env := Environment.new()
	var scene: String = args.get("scene", "default")
	var cam := Camera3D.new()
	cam.current = true
	root3d.add_child(cam)
	var kind: String = args.get("attrs", "physical")
	var attrs: CameraAttributes = null
	if kind == "physical":
		attrs = CameraAttributesPhysical.new()
	elif kind == "practical":
		attrs = CameraAttributesPractical.new()
	if scene == "default":
		_build_default(root3d, env)
	else:
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color(0, 0, 0)
		env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.ambient_light_color = Color(0, 0, 0)
		if scene == "points":
			pass # Built below, once the attributes set the field of view.
		elif scene == "ground":
			_build_ground(root3d, cam)
		elif scene == "halves":
			_build_halves(root3d)
	for k in args.keys():
		var key: String = k
		if key.begins_with("cam.") and attrs:
			attrs.set(key.substr(4), str_to_var(args[key]))
		elif key.begins_with("env."):
			env.set(key.substr(4), str_to_var(args[key]))
		elif key.begins_with("camera."):
			cam.set(key.substr(7), str_to_var(args[key]))
	if attrs:
		cam.attributes = attrs
	if scene == "points":
		var vfov := cam.fov
		if attrs is CameraAttributesPhysical:
			vfov = (attrs as CameraAttributesPhysical).get_fov()
		_build_points(root3d, vfov)
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)

func _build_default(root: Node3D, env: Environment) -> void:
	var sky_mat := ProceduralSkyMaterial.new()
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.rotation_degrees = Vector3(-50, 30, 0)
	root.add_child(sun)
	var ground := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(40, 40)
	ground.mesh = pm
	ground.position = Vector3(0, -1, 0)
	root.add_child(ground)
	for i in 5:
		var m := MeshInstance3D.new()
		m.mesh = BoxMesh.new() if i % 2 == 0 else SphereMesh.new()
		m.position = Vector3(-4 + i * 2, 0, -6 - i * 2)
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color.from_hsv(i / 5.0, 0.6, 0.9)
		if i == 2:
			mat.emission_enabled = true
			mat.emission = Color(1, 0.9, 0.7)
			mat.emission_energy_multiplier = 8.0
		m.material_override = mat
		root.add_child(m)

# Emissive points at frame-relative positions (x, y in -1..1 of the half extents) on a plane point_depth in front.
func _build_points(root: Node3D, vfov: float) -> void:
	var depth := float(args.get("point_depth", "20"))
	var size := float(args.get("point_size", "0.05"))
	var energy := float(args.get("point_energy", "50"))
	var pts: String = args.get("points", "0,0")
	var aspect := 16.0 / 9.0
	var half_h := depth * tan(deg_to_rad(vfov * 0.5))
	for p in pts.split(";"):
		var xy := p.split(",")
		var m := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = size
		sm.height = size * 2
		m.mesh = sm
		var mat := StandardMaterial3D.new()
		mat.albedo_color = Color(0, 0, 0)
		mat.metallic_specular = 0.0
		mat.emission_enabled = true
		mat.emission = Color(1, 1, 1)
		mat.emission_energy_multiplier = energy
		m.material_override = mat
		m.position = Vector3(float(xy[0]) * half_h * aspect, float(xy[1]) * half_h, -depth)
		root.add_child(m)

func _build_ground(root: Node3D, cam: Camera3D) -> void:
	var img := Image.create(256, 256, false, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			var c := 1.0 if ((x / 8 + y / 8) % 2 == 0) else 0.05
			img.set_pixel(x, y, Color(c, c, c))
	var tex := ImageTexture.create_from_image(img)
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_texture = tex
	mat.uv1_scale = Vector3(16, 16, 1)
	mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	var g := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(64, 64)
	g.mesh = pm
	g.material_override = mat
	root.add_child(g)
	cam.position = Vector3(0, float(args.get("cam_height", "1.5")), 0)
	cam.rotation_degrees = Vector3(float(args.get("cam_pitch", "-20")), 0, 0)

func _build_halves(root: Node3D) -> void:
	# The left half of the frame is a bright emissive card, the right half black.
	var m := MeshInstance3D.new()
	var qm := QuadMesh.new()
	qm.size = Vector2(40, 40)
	m.mesh = qm
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(float(args.get("card_value", "0.5")), float(args.get("card_value", "0.5")), float(args.get("card_value", "0.5")))
	m.material_override = mat
	m.position = Vector3(-20, 0, -10)
	root.add_child(m)

func _process(_delta: float) -> bool:
	frame += 1
	if frame == int(args.get("frames", "12")):
		var img := get_root().get_viewport().get_texture().get_image()
		if args.get("hdr", "0") == "1":
			# Raw float32 RGB beside the PNG name: <out>.f32, <out>.json with the size.
			img.convert(Image.FORMAT_RGBF)
			var out: String = args.get("out", "/tmp/out.png")
			var f := FileAccess.open(out + ".f32", FileAccess.WRITE)
			f.store_buffer(img.get_data())
			f.close()
			var j := FileAccess.open(out + ".json", FileAccess.WRITE)
			j.store_string(JSON.stringify({"w": img.get_width(), "h": img.get_height()}))
			j.close()
			img.convert(Image.FORMAT_RGBA8)
		img.save_png(args.get("out", "/tmp/out.png"))
		if args.has("exr"):
			pass
		return true
	return false
