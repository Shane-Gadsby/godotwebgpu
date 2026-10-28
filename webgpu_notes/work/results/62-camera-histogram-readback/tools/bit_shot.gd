extends SceneTree
# Renders a lit scene and saves the frame. Args: -- <out.png> <variant>; variant "plain" (defaults) or
# "drawn" (histogram + zebras drawn with Visible in Output, no script readback) or "nr" (noise reduction on).
func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	var out: String = args[0]
	var variant: String = args[1] if args.size() > 1 else "plain"
	var root3 := Node3D.new()
	root.add_child(root3)
	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = Sky.new()
	env.sky.sky_material = ProceduralSkyMaterial.new()
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	root3.add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	root3.add_child(sun)
	for i in 5:
		var mi := MeshInstance3D.new()
		mi.mesh = SphereMesh.new() if i % 2 == 0 else BoxMesh.new()
		mi.position = Vector3(i * 1.3 - 2.6, 0.5, -i * 0.5)
		var m := StandardMaterial3D.new()
		m.albedo_color = Color.from_hsv(i / 5.0, 0.7, 0.9)
		mi.material_override = m
		root3.add_child(mi)
	var floor_mi := MeshInstance3D.new()
	floor_mi.mesh = PlaneMesh.new()
	floor_mi.mesh.size = Vector2(20, 20)
	root3.add_child(floor_mi)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 2, 6)
	cam.rotation_degrees = Vector3(-15, 0, 0)
	root3.add_child(cam)
	cam.make_current()
	if variant == "drawn":
		cam.monitoring_visible_in_output = true
		cam.monitoring_histogram_enabled = true
		cam.monitoring_false_color_enabled = false # zebras crawl with time; histogram alone
	elif variant == "nr":
		var attr := CameraAttributesPractical.new()
		attr.noise_reduction_enabled = true
		cam.attributes = attr
	for i in 12:
		await process_frame
	root.get_texture().get_image().save_png(out)
	quit()
