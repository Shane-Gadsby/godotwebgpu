extends SceneTree
# By-eye frames for task 45: a lit scene with a sky and colored objects. Args: out=<png> look=off|teal_orange|bleach|warm
var frames := 0
var out := ""

func _init() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			args[kv[0]] = kv[1]
	out = args.get("out", "/tmp/look.png")
	var root3d := Node3D.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = Sky.new()
	env.sky.sky_material = ProceduralSkyMaterial.new()
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	var look: String = args.get("look", "off")
	if look != "off":
		env.grading_enabled = true
	if look == "teal_orange":
		env.grading_split_toning_shadows = Color(0.3, 0.5, 0.62)
		env.grading_split_toning_highlights = Color(0.68, 0.5, 0.32)
		env.grading_vibrance = 0.25
		env.grading_lift = Color(0.02, 0.02, 0.035)
		var c := Curve.new()
		c.add_point(Vector2(0, 0.03))
		c.add_point(Vector2(0.25, 0.19))
		c.add_point(Vector2(0.75, 0.84))
		c.add_point(Vector2(1, 0.97))
		env.grading_curve_master = c
	elif look == "bleach":
		env.grading_vibrance = -0.6
		env.grading_gain = Color(1.08, 1.08, 1.08)
		env.grading_gamma = Color(0.9, 0.9, 0.9)
		var c2 := Curve.new()
		c2.add_point(Vector2(0, 0.0))
		c2.add_point(Vector2(0.3, 0.2))
		c2.add_point(Vector2(0.7, 0.85))
		c2.add_point(Vector2(1, 1))
		env.grading_curve_master = c2
	elif look == "warm":
		env.grading_color_filter = Color(1.0, 0.9, 0.75)
		env.grading_hue_shift = -5.0
		env.grading_shadows = Color(0.95, 0.95, 1.1)
		env.grading_highlights = Color(1.1, 1.0, 0.9)
	var we := WorldEnvironment.new()
	we.environment = env
	root3d.add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 30, 0)
	sun.shadow_enabled = true
	root3d.add_child(sun)
	var floor_mi := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(20, 20)
	floor_mi.mesh = pm
	var fm := StandardMaterial3D.new()
	fm.albedo_color = Color(0.55, 0.55, 0.5)
	floor_mi.material_override = fm
	root3d.add_child(floor_mi)
	var cols := [Color(0.9, 0.15, 0.1), Color(0.95, 0.75, 0.2), Color(0.2, 0.7, 0.25), Color(0.15, 0.4, 0.9), Color(0.85, 0.6, 0.5), Color(0.9, 0.9, 0.9), Color(0.1, 0.1, 0.1)]
	for i in cols.size():
		var mi := MeshInstance3D.new()
		mi.mesh = SphereMesh.new()
		var m := StandardMaterial3D.new()
		m.albedo_color = cols[i]
		m.roughness = 0.4
		mi.material_override = m
		mi.position = Vector3(-3.6 + i * 1.2, 0.5, 0)
		root3d.add_child(mi)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 2.0, 5.5)
	cam.rotation_degrees = Vector3(-15, 0, 0)
	root3d.add_child(cam)
	root.add_child(root3d)

func _process(_d: float) -> bool:
	frames += 1
	if frames == 15:
		root.get_viewport().get_texture().get_image().save_png(out)
		quit()
	return false
